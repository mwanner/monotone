// Copyright (C) 2008, 2010, 2012 - 2014 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2008 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "merge_content.hh"

#include "constants.hh"
#include "database.hh"
#include "diff_output.hh"
#include "file_io.hh"
#include "lua_hooks.hh"
#include "revision.hh"
#include "merge_roster.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "xdelta.hh"

#include "safe_map.hh"
#include "vector.hh"
#include <iostream>
#include <memory>

using std::move;
using std::make_pair;
using std::make_shared;
using std::map;
using std::set;
using std::shared_ptr;
using std::string;
using std::vector;

///////////////////////////////////////////////////////////////////////////
// content_merge_database_adaptor
///////////////////////////////////////////////////////////////////////////

content_merge_database_adaptor::content_merge_database_adaptor(database & db,
                                                               revision_id const & left,
                                                               revision_id const & right,
                                                               marking_map const & left_mm,
                                                               marking_map const & right_mm,
                                                               set<revision_id> left_uncommon_ancestors,
                                                               set<revision_id> right_uncommon_ancestors)
  : db(db), left_rid (left), right_rid (right), left_mm(left_mm), right_mm(right_mm),
  left_uncommon_ancestors (left_uncommon_ancestors), right_uncommon_ancestors (right_uncommon_ancestors)
{
  // FIXME: possibly refactor to run this lazily, as we don't
  // need to find common ancestors if we're never actually
  // called on to do content merging.
  find_common_ancestor_for_merge(db, left, right, lca);
}

void
content_merge_database_adaptor::record_merge(file_id const & left_ident,
                                             file_id const & right_ident,
                                             file_id const & merged_ident,
                                             file_data const & left_data,
                                             file_data const & right_data,
                                             file_data const & merged_data)
{
  L(FL("recording successful merge of %s <-> %s into %s")
    % left_ident
    % right_ident
    % merged_ident);

  transaction_guard guard(db);

  if (!(left_ident == merged_ident))
    {
      delta left_delta;
      diff(left_data.inner(), merged_data.inner(), left_delta);
      db.put_file_version(left_ident, merged_ident, file_delta(left_delta));
    }
  if (!(right_ident == merged_ident))
    {
      delta right_delta;
      diff(right_data.inner(), merged_data.inner(), right_delta);
      db.put_file_version(right_ident, merged_ident, file_delta(right_delta));
    }
  guard.commit();
}

void
content_merge_database_adaptor::record_file(file_id const & ident,
                                            file_data const & data)
{
  L(FL("recording new file %s")
    % ident);

  transaction_guard guard(db);

  db.put_file(ident, data);

  guard.commit();
}

void
content_merge_database_adaptor::record_file(file_id const & parent_ident,
                                            file_id const & merged_ident,
                                            file_data const & parent_data,
                                            file_data const & merged_data)
{
  L(FL("recording file %s -> %s")
    % parent_ident
    % merged_ident);

  transaction_guard guard(db);

  if (!(parent_ident == merged_ident))
    {
      delta parent_delta;
      diff(parent_data.inner(), merged_data.inner(), parent_delta);
      db.put_file_version(parent_ident, merged_ident, file_delta(parent_delta));
    }
  guard.commit();
}

void
content_merge_database_adaptor::cache_roster(revision_id const & rid,
                                             roster_t_cp roster)
{
  safe_insert(rosters, make_pair(rid, roster));
};

static void
load_and_cache_roster(database & db, revision_id const & rid,
                      map<revision_id, roster_t_cp> & rmap,
                      roster_t_cp & rout)
{
  map<revision_id, roster_t_cp>::const_iterator i = rmap.find(rid);
  if (i != rmap.end())
    rout = i->second;
  else
    {
      cached_roster cr = db.get_cached_roster(rid);
      safe_insert(rmap, make_pair(rid, cr.first));
      rout = cr.first;
    }
}

roster_t_cp
content_merge_database_adaptor::get_ancestral_roster(node_id nid,
                                                     revision_id & rid)
{
  roster_t_cp anc;

  // Given a file, if the lca is nonzero and its roster contains the file,
  // then we use its roster.  Otherwise we use the roster at the file's
  // birth revision, which is the "per-file worst case" lca.

  // Begin by loading any non-empty file lca roster
  rid = lca;
  if (!lca.inner()().empty())
    load_and_cache_roster(db, lca, rosters, anc);

  // If there is no LCA, or the LCA's roster doesn't contain the file,
  // then use the file's birth roster.
  if (!anc || !anc->has_node(nid))
    {
      if (!left_mm.contains(nid))
        {
          rid = right_mm.get_marking(nid)->birth_revision;
        }
      else if (!right_mm.contains(nid))
        {
          rid = left_mm.get_marking(nid)->birth_revision;
        }
      else
        {
          const_marking_t const & lm = left_mm.get_marking(nid);
          const_marking_t const & rm = right_mm.get_marking(nid);
          I(lm->birth_revision == rm->birth_revision);
          rid = lm->birth_revision;
        }

      load_and_cache_roster(db, rid, rosters, anc);
    }
  I(anc);
  return anc;
}

void
content_merge_database_adaptor::get_dropped_details(set<revision_id> const & uncommon_ancestors,
                                                    revision_id const &      least_common_ancestor,
                                                    node_id const            nid,
                                                    revision_id &            dropped_rev_id,
                                                    file_path   &            dropped_name,
                                                    file_id     &            dropped_file_id)
{
  roster_t roster;
  marking_map marking_map;

  // graph.cc ensures that uncommon_ancestors is in topological order, leaf-most first.
  for (set<revision_id>::const_iterator i = uncommon_ancestors.begin();
       i != uncommon_ancestors.end(); ++i)
    {
      db.get_roster_and_markings(*i, roster, marking_map);
      if (roster.has_node(nid))
        {
          dropped_rev_id = *i;
          roster.get_file_details(nid, dropped_file_id, dropped_name);
          return;
        }
    }

  db.get_roster_and_markings(least_common_ancestor, roster, marking_map);
  if (roster.has_node(nid))
    {
      dropped_rev_id = least_common_ancestor;
      roster.get_file_details(nid, dropped_file_id, dropped_name);
      return;
    }
  I(false);
}

file_data
content_merge_database_adaptor::get_version(file_id const & ident) const
{
  return db.get_file_version(ident);
}


///////////////////////////////////////////////////////////////////////////
// content_merge_workspace_adaptor
///////////////////////////////////////////////////////////////////////////

void
content_merge_workspace_adaptor::cache_roster(revision_id const & rid,
                                              roster_t_cp roster)
{
  rosters.insert(std::make_pair(rid, roster));
}

void
content_merge_workspace_adaptor::record_merge(file_id const & left_id,
                                              file_id const & right_id,
                                              file_id const & merged_id,
                                              file_data const & /* left_data */ ,
                                              file_data const & /* right_data */ ,
                                              file_data const & merged_data)
{
  L(FL("temporarily recording merge of %s <-> %s into %s")
    % left_id
    % right_id
    % merged_id);
  // this is an insert instead of a safe_insert because it is perfectly
  // legal (though rare) to have multiple merges resolve to the same file
  // contents.
  temporary_store.insert(make_pair(merged_id, merged_data));
}

void
content_merge_workspace_adaptor::record_file(file_id const & id,
                                             file_data const & data)
{
  L(FL("temporarily recording file %s")
    % id);
  // this is an insert instead of a safe_insert because it is perfectly
  // legal (though rare) to have multiple merges resolve to the same file
  // contents.
  temporary_store.insert(make_pair(id, data));
}

void
content_merge_workspace_adaptor::record_file(file_id const & parent_id,
                                             file_id const & merged_id,
                                             file_data const & /* parent_data */ ,
                                             file_data const & merged_data)
{
  L(FL("temporarily recording file %s -> %s")
    % parent_id
    % merged_id);
  // this is an insert instead of a safe_insert because it is perfectly
  // legal (though rare) to have multiple merges resolve to the same file
  // contents.
  temporary_store.insert(make_pair(merged_id, merged_data));
}

roster_t_cp
content_merge_workspace_adaptor::get_ancestral_roster(node_id nid,
                                                      revision_id & rid)
{
  roster_t_cp anc;

  // Begin by loading any non-empty file lca roster
  if (base->has_node(nid))
    {
      rid = lca;
      return base;
    }
  else
    {
      if (!left_mm.contains(nid))
        rid = right_mm.get_marking(nid)->birth_revision;
      else if (!right_mm.contains(nid))
        rid = left_mm.get_marking(nid)->birth_revision;
      else
        {
          const_marking_t const & lm = left_mm.get_marking(nid);
          const_marking_t const & rm = right_mm.get_marking(nid);
          I(lm->birth_revision == rm->birth_revision);
          rid = lm->birth_revision;
        }

      roster_t_cp anc;
      load_and_cache_roster(db, rid, rosters, anc);
      I(anc);
      return anc;
    }
}

file_data
content_merge_workspace_adaptor::get_version(file_id const & ident) const
{
  map<file_id,file_data>::const_iterator i = temporary_store.find(ident);
  if (i != temporary_store.end())
    return i->second;
  else if (db.file_version_exists(ident))
    return db.get_file_version(ident);
  else
    {
      data tmp;
      file_id fid;
      map<file_id, file_path>::const_iterator i = content_paths.find(ident);
      I(i != content_paths.end());

      require_path_is_file(i->second,
        F("file '%s' does not exist in workspace") % i->second,
        F("'%s' in workspace is a directory, not a file") % i->second,
        F("'%s' in workspace is not a regular file") % i->second);
      tmp = read_data(i->second);
      fid = calculate_ident(file_data(tmp));
      E(fid == ident, origin::system,
        F("file '%s' in workspace has id %s, wanted %s")
        % i->second
        % fid
        % ident);
      return file_data(move(tmp));
    }
}


///////////////////////////////////////////////////////////////////////////
// content_merge_checkout_adaptor
///////////////////////////////////////////////////////////////////////////

void
content_merge_checkout_adaptor::record_merge(file_id const & /* left_ident */ ,
                                             file_id const & /* right_ident */ ,
                                             file_id const & /* merged_ident */ ,
                                             file_data const & /* left_data */ ,
                                             file_data const & /* right_data */ ,
                                             file_data const & /* merged_data */ )
{
  I(false);
}

void
content_merge_checkout_adaptor::record_file(file_id const & /* parent_ident */ ,
                                            file_id const & /* merged_ident */ ,
                                            file_data const & /* parent_data */ ,
                                            file_data const & /* merged_data */ )
{
  I(false);
}

void
content_merge_checkout_adaptor::record_file(file_id const & /* ident */ ,
                                            file_data const & /* data */ )
{
  I(false);
}

roster_t_cp
content_merge_checkout_adaptor::get_ancestral_roster(node_id /* nid */ ,
                                                     revision_id & /* rid */)
{
  I(false);
}

file_data
content_merge_checkout_adaptor::get_version(file_id const & ident) const
{
  return db.get_file_version(ident);
}

///////////////////////////////////////////////////////////////////////////
// content_merge_empty_adaptor
///////////////////////////////////////////////////////////////////////////

void
content_merge_empty_adaptor::record_merge(file_id const & /* left_ident */ ,
                                          file_id const & /* right_ident */ ,
                                          file_id const & /* merged_ident */ ,
                                          file_data const & /* left_data */ ,
                                          file_data const & /* right_data */ ,
                                          file_data const & /* merged_data */ )
{
  I(false);
}

void
content_merge_empty_adaptor::record_file(file_id const & /* ident */ ,
                                         file_data const & /* data */ )
{
  I(false);
}

void
content_merge_empty_adaptor::record_file(file_id const & /* parent_ident */ ,
                                         file_id const & /* merged_ident */ ,
                                         file_data const & /* parent_data */ ,
                                         file_data const & /* merged_data */ )
{
  I(false);
}

roster_t_cp
content_merge_empty_adaptor::get_ancestral_roster(node_id /* nid */ ,
                                                  revision_id & /* rid */)
{
  I(false);
}

file_data
content_merge_empty_adaptor::get_version(file_id const & /* ident */) const
{
  I(false);
}

///////////////////////////////////////////////////////////////////////////
// content_merger
///////////////////////////////////////////////////////////////////////////

string
content_merger::get_file_encoding(file_path const & path,
                                  roster_t const & ros)
{
  attr_value v;
  if (ros.get_attr(path, attr_key(constants::encoding_attribute), v))
    return v();
  return constants::default_encoding;
}

bool
content_merger::attribute_manual_merge(file_path const & path,
                                       roster_t const & ros)
{
  attr_value v;
  if (ros.get_attr(path, attr_key(constants::manual_merge_attribute), v)
      && v() == "true")
    return true;
  return false; // default: enable auto merge
}

bool
content_merger::attempt_auto_merge(file_path const & anc_path, // inputs
                                   file_path const & left_path,
                                   file_path const & right_path,
                                   file_id const & ancestor_id,
                                   file_id const & left_id,
                                   file_id const & right_id,
                                   file_data & left_data, // outputs
                                   file_data & right_data,
                                   file_data & merge_data)
{
  I(left_id != right_id);

  if (attribute_manual_merge(left_path, left_ros) ||
      attribute_manual_merge(right_path, right_ros))
    {
      return false;
    }

  // both files mergeable by monotone internal algorithm, try to merge
  // note: the ancestor is not considered for manual merging. Forcing the
  // user to merge manually just because of an ancestor mistakenly marked
  // manual seems too harsh

  file_data ancestor_data = adaptor.get_version(ancestor_id);
  left_data = adaptor.get_version(left_id);
  right_data = adaptor.get_version(right_id);

  data const left_unpacked = left_data.inner();
  data const ancestor_unpacked = ancestor_data.inner();
  data const right_unpacked = right_data.inner();

  string const left_encoding(get_file_encoding(left_path, left_ros));
  string const anc_encoding(get_file_encoding(anc_path, anc_ros));
  string const right_encoding(get_file_encoding(right_path, right_ros));

  vector<string> left_lines, ancestor_lines, right_lines, merged_lines;
  split_into_lines(left_unpacked(), left_encoding, left_lines,
                   split_flags::keep_endings);
  split_into_lines(ancestor_unpacked(), anc_encoding, ancestor_lines,
                   split_flags::keep_endings);
  split_into_lines(right_unpacked(), right_encoding, right_lines,
                   split_flags::keep_endings);

  if (merge3(ancestor_lines, left_lines, right_lines, merged_lines))
    {
      string tmp;

      join_lines(merged_lines, tmp, "");
      merge_data = file_data(tmp, origin::internal);
      return true;
    }

  return false;
}

bool
content_merger::try_auto_merge(file_path const & anc_path,
                               file_path const & left_path,
                               file_path const & right_path,
                               file_path const & merged_path,
                               file_id const & ancestor_id,
                               file_id const & left_id,
                               file_id const & right_id,
                               file_id & merged_id)
{
  // This version of try_to_merge_files should only be called when there is a
  // real merge3 to perform.
  I(!null_id(ancestor_id));
  I(!null_id(left_id));
  I(!null_id(right_id));

  L(FL("trying auto merge '%s' %s <-> %s (ancestor: %s)")
    % merged_path
    % left_id
    % right_id
    % ancestor_id);

  if (left_id == right_id)
    {
      L(FL("files are identical"));
      merged_id = left_id;
      return true;
    }

  file_data left_data, right_data, merge_data;

  if (attempt_auto_merge(anc_path, left_path, right_path,
                         ancestor_id, left_id, right_id,
                         left_data, right_data, merge_data))
    {
      L(FL("internal 3-way merged ok"));
      merged_id = calculate_ident(merge_data);

      adaptor.record_merge(left_id, right_id, merged_id,
                           left_data, right_data, merge_data);

      return true;
    }

  return false;
}

bool
content_merger::try_user_merge(file_path const & anc_path,
                               file_path const & left_path,
                               file_path const & right_path,
                               file_path const & merged_path,
                               file_id const & ancestor_id,
                               file_id const & left_id,
                               file_id const & right_id,
                               file_id & merged_id)
{
  // This version of try_to_merge_files should only be called when there is a
  // real merge3 to perform.
  I(!null_id(ancestor_id));
  I(!null_id(left_id));
  I(!null_id(right_id));

  L(FL("trying user merge '%s' %s <-> %s (ancestor: %s)")
    % merged_path
    % left_id
    % right_id
    % ancestor_id);

  if (left_id == right_id)
    {
      L(FL("files are identical"));
      merged_id = left_id;
      return true;
    }

  file_data
    ancestor_data = adaptor.get_version(ancestor_id),
    left_data = adaptor.get_version(left_id),
    right_data = adaptor.get_version(right_id);
  data
    ancestor_unpacked = ancestor_data.inner(),
    left_unpacked = left_data.inner(),
    right_unpacked = right_data.inner(),
    merged_unpacked;

  P(F("help required for 3-way merge\n"
      "[ancestor] %s\n"
      "[    left] %s\n"
      "[   right] %s\n"
      "[  merged] %s")
    % anc_path
    % left_path
    % right_path
    % merged_path);

  if (lua.hook_merge3(anc_path, left_path, right_path, merged_path,
                      ancestor_unpacked, left_unpacked,
                      right_unpacked, merged_unpacked))
    {
      file_data merge_data(merged_unpacked);

      L(FL("lua merge3 hook merged ok"));
      merged_id = calculate_ident(merge_data);

      adaptor.record_merge(left_id, right_id, merged_id,
                           left_data, right_data, merge_data);
      return true;
    }

  return false;
}

enum merge_method { auto_merge, user_merge };

static void
try_to_merge_files(lua_hooks & lua,
                   roster_t const & left_roster, roster_t const & right_roster,
                   roster_merge_result & result, content_merge_adaptor & adaptor,
                   merge_method const method)
{
  size_t cnt;
  size_t total_conflicts = result.file_content_conflicts.size();
  std::vector<file_content_conflict>::iterator it;

  for (cnt = 1, it = result.file_content_conflicts.begin();
       it != result.file_content_conflicts.end(); ++cnt)
    {
      file_content_conflict const & conflict = *it;

      MM(conflict);

      revision_id rid;
      roster_t_cp roster_for_file_lca
        = adaptor.get_ancestral_roster(conflict.nid, rid);

      // Now we should certainly have a roster, which has the node.
      I(roster_for_file_lca);
      I(roster_for_file_lca->has_node(conflict.nid));

      file_id anc_id, left_id, right_id;
      file_path anc_path, left_path, right_path;
      roster_for_file_lca->get_file_details(conflict.nid, anc_id, anc_path);
      left_roster.get_file_details(conflict.nid, left_id, left_path);
      right_roster.get_file_details(conflict.nid, right_id, right_path);

      file_id merged_id;

      content_merger cm(lua, *roster_for_file_lca,
                        left_roster, right_roster,
                        adaptor);

      bool merged = false;

      switch (method)
        {
        case auto_merge:
          merged = cm.try_auto_merge(anc_path, left_path, right_path,
                                     right_path, anc_id, left_id, right_id,
                                     merged_id);
          break;

        case user_merge:
          merged = cm.try_user_merge(anc_path, left_path, right_path,
                                     right_path, anc_id, left_id, right_id,
                                     merged_id);

          // If the user merge has failed, there's no point
          // trying to continue -- we'll only frustrate users by
          // encouraging them to continue working with their merge
          // tool on a merge that is now destined to fail.
          if (!merged)
            return;

          break;
        }

      if (merged)
        {
          L(FL("resolved content conflict %d / %d on file '%s'")
            % cnt % total_conflicts % right_path);
          file_t f = downcast_to_file_t(result.roster.get_node_for_update(conflict.nid));
          f->content = merged_id;

          it = result.file_content_conflicts.erase(it);
        }
      else
        {
          ++it;
        }
    }
}

void
resolve_merge_conflicts(lua_hooks & lua,
                        options const & opts,
                        roster_t const & left_roster,
                        roster_t const & right_roster,
                        roster_merge_result & result,
                        content_merge_adaptor & adaptor,
                        temp_node_id_source & nis,
                        const bool resolutions_given)
{
  if (!result.is_clean())
    {
      result.log_conflicts();

      if (resolutions_given)
        {
          // We require --resolve-conflicts to enable processing attr
          // mtn:resolve_conflict.

          // If there are any conflicts for which we don't currently support
          // resolutions, give a nice error message.
          char const * const msg = "conflict resolution for %s not yet supported";

          E(!result.missing_root_conflict, origin::user,
            F(msg) % "missing_root_dir");
          E(result.invalid_name_conflicts.size() == 0, origin::user,
            F(msg) % "invalid_name_conflicts");
          E(result.directory_loop_conflicts.size() == 0, origin::user,
            F(msg) % "directory_loop_conflicts");
          E(result.multiple_name_conflicts.size() == 0, origin::user,
            F(msg) % "multiple_name_conflicts");
          E(result.attribute_conflicts.size() == 0, origin::user,
            F(msg) % "attribute_conflicts");

          // Resolve the ones we can, if they have resolutions specified. Each
          // conflict list is deleted once all are resolved.
          result.resolve_orphaned_node_conflicts(lua, left_roster, right_roster);
          result.resolve_dropped_modified_conflicts(lua, left_roster, right_roster,
                                                    dynamic_cast <content_merge_database_adaptor&>(adaptor), nis);
          result.resolve_duplicate_name_conflicts(lua, left_roster, right_roster, adaptor);

          result.resolve_file_content_conflicts (lua, left_roster, right_roster, adaptor);
        }
    }

  if (result.has_non_content_conflicts())
    {
      result.report_missing_root_conflicts(left_roster, right_roster, adaptor, false, std::cout);
      result.report_invalid_name_conflicts(left_roster, right_roster, adaptor, false, std::cout);
      result.report_directory_loop_conflicts(left_roster, right_roster, adaptor, false, std::cout);

      result.report_orphaned_node_conflicts(left_roster, right_roster, adaptor, false, std::cout);
      result.report_multiple_name_conflicts(left_roster, right_roster, adaptor, false, std::cout);
      result.report_dropped_modified_conflicts(left_roster, right_roster, adaptor, false, std::cout);
      result.report_duplicate_name_conflicts(left_roster, right_roster, adaptor, false, std::cout);

      result.report_attribute_conflicts(left_roster, right_roster, adaptor, false, std::cout);
      result.report_file_content_conflicts(lua, left_roster, right_roster, adaptor, false, std::cout);
    }
  else if (result.has_content_conflicts())
    {
      // Attempt to auto-resolve any content conflicts using the line-merger.
      // To do this requires finding a merge ancestor.

      L(FL("examining content conflicts"));

      try_to_merge_files(lua, left_roster, right_roster,
                         result, adaptor, auto_merge);

      size_t remaining = result.file_content_conflicts.size();
      if (remaining > 0)
        {
          P(FP("%d content conflict requires user intervention",
               "%d content conflicts require user intervention",
               remaining) % remaining);

          // We don't spawn the merger here, because some mergers
          // (such as opendiff) require prompting the user via
          // stdin/stdout, and that's what 'non_interactive' prevents.
          // Note that 'automate stdio' sets non_interactive true,
          // because it doesn't support prompting.
          //
          // Another option would be to pass the option to the merger,
          // and let it decide. We are not doing that, because it is
          // felt this whole design is already too complicated; we are
          // working to find a better solution. See thread at
          // http://lists.gnu.org/archive/html/monotone-devel/2010-04/msg00000.html
          E(!opts.non_interactive, origin::user,
            F("can't spawn external merger when non-interactive"));

          result.report_file_content_conflicts(lua, left_roster, right_roster, adaptor, false, std::cout);

          try_to_merge_files(lua, left_roster, right_roster,
                             result, adaptor, user_merge);
        }
    }

  E(result.is_clean(), origin::user,
    F("merge failed due to unresolved conflicts"));
}

void
interactive_merge_and_store(lua_hooks & lua,
                            database & db,
                            options const & opts,
                            revision_id const & left_rid,
                            revision_id const & right_rid,
                            revision_id & merged_rid)
{
  roster_t left_roster, right_roster;
  marking_map left_marking_map, right_marking_map;
  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;

  db.get_roster_and_markings(left_rid, left_roster, left_marking_map);
  db.get_roster_and_markings(right_rid, right_roster, right_marking_map);
  db.get_uncommon_ancestors(left_rid, right_rid,
                            left_uncommon_ancestors, right_uncommon_ancestors);

  roster_merge_result result;

  roster_merge(left_roster, left_marking_map, left_uncommon_ancestors,
               right_roster, right_marking_map, right_uncommon_ancestors,
               result);

  bool resolutions_given;
  temp_node_id_source nis;
  content_merge_database_adaptor dba(db, left_rid, right_rid,
                                     left_marking_map, right_marking_map,
                                     left_uncommon_ancestors, right_uncommon_ancestors);

  parse_resolve_conflicts_opts (opts, left_rid, left_roster, right_rid, right_roster, result, resolutions_given);

  resolve_merge_conflicts(lua, opts, left_roster, right_roster, result, dba, nis, resolutions_given);

  // write new files into the db
  store_roster_merge_result(db,
                            left_roster, right_roster, result,
                            left_rid, right_rid, merged_rid);
}

void
store_roster_merge_result(database & db,
                          roster_t const & left_roster,
                          roster_t const & right_roster,
                          roster_merge_result & result,
                          revision_id const & left_rid,
                          revision_id const & right_rid,
                          revision_id & merged_rid)
{
  I(result.is_clean());
  roster_t & merged_roster = result.roster;
  merged_roster.check_sane(true); // resolve conflicts can create new nodes

  revision_t merged_rev;
  merged_rev.made_for = made_for_database;

  merged_rev.new_manifest = calculate_ident(merged_roster);

  shared_ptr<cset> left_to_merged = make_shared<cset>
    (left_roster, merged_roster);
  safe_insert(merged_rev.edges, make_pair(left_rid, left_to_merged));

  shared_ptr<cset> right_to_merged = make_shared<cset>
    (right_roster, merged_roster);
  safe_insert(merged_rev.edges, make_pair(right_rid, right_to_merged));

  revision_data merged_data = write_revision(merged_rev);
  merged_rid = calculate_ident(merged_data);
  {
    transaction_guard guard(db);

    db.put_revision(merged_rid, move(merged_rev));

    guard.commit();
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
