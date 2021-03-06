// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __OPTION_HH__
#define __OPTION_HH__

/*
 * Infrastructure for parsing options.
 *
 * This can be used on its own with concrete_option_set::operator()(), or
 * used with something like options.{cc,hh} and option_set. The former is
 * very simple to do, while the latter should allow slightly better code
 * structure for more involved uses.
 */

#include <stdexcept>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include "vector.hh"

#include "lexical_cast.hh"

#include "sanity.hh"
#include "vocab.hh"

// The types to represent the command line's parameters.
class arg_type : public utf8 {
public:
  explicit arg_type(void) : utf8() {}
  arg_type(std::string const & s, origin::type f) : utf8(s, f) {}
  explicit arg_type(utf8 const & u) : utf8(u) {}
};
template <>
inline void dump(arg_type const & a, std::string & out) { out = a(); }
typedef std::vector< arg_type > args_vector;

namespace option {
  // Base for errors thrown by this code.
  struct option_error : public std::invalid_argument
  {
    option_error(std::string const & str);
  };
  struct unknown_option : public option_error
  {
    unknown_option(std::string const & opt);
  };
  struct missing_arg : public option_error
  {
    missing_arg(std::string const & opt);
  };
  // -ofoo or --opt=foo when the option doesn't take an argument
  struct extra_arg : public option_error
  {
    extra_arg(std::string const & opt);
  };
  // thrown by from_command_line when setting an option fails
  // by either boost::bad_lexical_cast or bad_arg_internal
  struct bad_arg : public option_error
  {
    bad_arg(std::string const & opt, arg_type const & arg);
    bad_arg(std::string const & opt,
            arg_type const & arg,
            std::string const & reason);
  };
  // from_command_line() catches this and boost::bad_lexical_cast
  // and converts them to bad_arg exceptions
  struct bad_arg_internal
  {
    std::string reason;
    bad_arg_internal(std::string const & str = "");
  };

  // Split a "long,s/cancel" option name into long and short names.
  void splitname(char const * from, std::string & name, std::string & n, std::string & cancelname);

  // An option that can be set and reset.
  struct concrete_option
  {
    char const * description;
    std::string longname;
    std::string shortname;
    std::string cancelname;
    bool has_arg;
    std::function<void (std::string)> setter;
    std::function<void ()> resetter;
    bool hidden;
    char const * deprecated;

    concrete_option();
    concrete_option(char const * names,
                    char const * desc,
                    bool arg,
                    std::function<void (std::string)> set,
                    std::function<void ()> reset,
                    bool hide = false,
                    char const * deprecate = 0);

    bool operator<(concrete_option const & other) const;
  };

  // A group of options, which can be set from a command line
  // and can produce a usage string.
  struct concrete_option_set
  {
    std::set<concrete_option> options;
    concrete_option_set();
    concrete_option_set(std::set<concrete_option> const & other);
    concrete_option_set(concrete_option const & opt);

    // for building a concrete_option_set directly (as done in unit_tests.cc),
    // rather than using intermediate machinery like in options*
    concrete_option_set &
    operator()(char const * names,
               char const * desc,
               std::function<void ()> set,
               std::function<void ()> reset = 0,
               bool hide = false,
               char const * deprecate = 0);
    concrete_option_set &
    operator()(char const * names,
               char const * desc,
               std::function<void (std::string)> set,
               std::function<void ()> reset = 0,
               bool hide = false,
               char const * deprecate = 0);

    concrete_option_set operator | (concrete_option_set const & other) const;
    void reset() const;
    void get_usage_strings(std::vector<std::string> & names,
                           std::vector<std::string> & descriptions,
                           unsigned int & maxnamelen,
                           bool show_hidden
                           /*no way to see deprecated*/) const;

    enum preparse_flag { no_preparse, preparse };
    void from_command_line(args_vector & args,
                           preparse_flag pf = no_preparse);
    // Does not allow --xargs
    void from_command_line(int argc,
                           char const * const * argv);
    typedef std::pair<std::string, std::string> key_value_pair;
    typedef std::vector<key_value_pair> key_value_list;
    // Does not allow --xargs
    void from_key_value_pairs(key_value_list const & keyvals);
  };
  concrete_option_set
  operator | (concrete_option const & a, concrete_option const & b);

  // used by the setter() functions below
  template<typename T>
  struct setter_class
  {
    T & item;
    setter_class(T & i)
      : item(i)
    {}
    void operator()(std::string s)
    {
      item = boost::lexical_cast<T>(s);
    }
  };
  template<>
  struct setter_class<bool>
  {
    bool & item;
    setter_class(bool & i)
      : item(i)
    {}
    void operator()()
    {
      item = true;
    }
  };
  template<typename T>
  struct setter_class<std::vector<T> >
  {
    std::vector<T> & items;
    setter_class(std::vector<T> & i)
      : items(i)
    {}
    void operator()(std::string s)
    {
      items.push_back(boost::lexical_cast<T>(s));
    }
  };
  template<typename T>
  struct resetter_class
  {
    T & item;
    T value;
    resetter_class(T & i, T const & v)
      : item(i), value(v)
    {}
    void operator()()
    {
      item = value;
    }
  };

  // convenience functions to generate a setter for a var
  template<typename T> inline
  std::function<void(std::string)> setter(T & item)
  {
    return setter_class<T>(item);
  }
  inline std::function<void()> setter(bool & item)
  {
    return setter_class<bool>(item);
  }
  // convenience function to generate a resetter for a var
  template<typename T> inline
  std::function<void()> resetter(T & item, T const & value = T())
  {
    return resetter_class<T>(item, value);
  }

  // because std::bind1st can't handle producing a nullary functor
  template<typename T>
  struct binder_only
  {
    T * obj;
    std::function<void(T*)> fun;
    binder_only(std::function<void(T*)> const & f, T * o)
      : obj(o), fun(f)
      {}
    void operator()()
    {
      fun(obj);
    }
  };

  // Options that need to be attached to some other object
  // in order for set and reset to be meaningful.
  template<typename T>
  struct option
  {
    char const * description;
    char const * names;
    bool has_arg;
    std::function<void (T*, std::string)> setter;
    std::function<void (T*)> resetter;
    bool hidden;
    char const * deprecated;

    option(char const * name,
           char const * desc,
           bool arg,
           void(T::*set)(std::string),
           void(T::*reset)(),
           bool hide,
           char const * deprecate)
    {
      I((name && name[0]) || (desc && desc[0]));
      description = desc;
      names = name;
      has_arg = arg;
      setter = set;
      resetter = reset;
      hidden = hide;
      deprecated = deprecate;
    }

    concrete_option instantiate(T * obj) const
    {
      concrete_option out;
      out.description = description;
      splitname(names, out.longname, out.shortname, out.cancelname);
      out.has_arg = has_arg;

      if (setter)
        out.setter = std::bind1st(setter, obj);
      if (resetter)
        out.resetter = binder_only<T>(resetter, obj);

      out.hidden = hidden;
      out.deprecated = deprecated;

      return out;
    }

    bool operator<(option const & other) const
    {
      if (names != other.names)
        return names < other.names;
      return description < other.description;
    }
  };

  // A group of unattached options, which can be given an object
  // to attach themselves to.
  template<typename T>
  struct option_set
  {
    std::set<option<T> > options;
    option_set(){}
    option_set(option_set<T> const & other)
      : options(other.options)
    {}
    option_set(option<T> const & opt)
    {
      options.insert(opt);
    }

    option_set(char const * name,
               char const * desc,
               bool arg,
               void(T::*set)(std::string),
               void(T::*reset)(),
               bool hidden = false,
               char const * deprecated = 0)
    {
      options.insert(option<T>(name, desc, arg, set, reset, hidden, deprecated));
    }
    concrete_option_set instantiate(T * obj) const
    {
      std::set<concrete_option> out;
      for (typename std::set<option<T> >::const_iterator i = options.begin();
           i != options.end(); ++i)
        out.insert(i->instantiate(obj));
      return out;
    }
    option_set<T> operator | (option_set<T> const & other) const
    {
      option_set<T> combined;
      std::set_union(options.begin(), options.end(),
                     other.options.begin(), other.options.end(),
                     std::inserter(combined.options, combined.options.begin()));
      return combined;
    }
    option_set<T> operator - (option_set<T> const & other) const
    {
      option_set<T> combined;
      std::set_difference(options.begin(), options.end(),
                          other.options.begin(), other.options.end(),
                          std::inserter(combined.options,
                                        combined.options.begin()));
      return combined;
    }
    bool empty() const {return options.empty();}
  };
  template<typename T>
  option_set<T>
  operator | (option<T> const & a, option<T> const & b)
  {
    return option_set<T>(a) | b;
  }

}


#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
