// copyright (C) 2006 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

#include <boost/regex.hpp>

#include "sanity.hh"
#include "uri.hh"

using std::string;

bool
parse_uri(string const & in, uri & out)
{
  uri u;

  // This is a simplified URI grammar. It does the basics.

  string scheme_part = "(?:([^:/?#]+):)?";
  string authority_part = "(?://([^/?#]*))?";
  string path_part = "([^?#]*)";
  string query_part = "(?:\\?([^#]*))?";
  string fragment_part = "(?:#(.*))?";

  string uri_rx = (string("^")
		   + scheme_part
		   + authority_part
		   + path_part
		   + query_part
		   + fragment_part
		   + "$");

  boost::match_results<std::string::const_iterator> uri_matches;
  if (boost::regex_match(in, uri_matches, boost::regex(uri_rx)))
    {

      u.scheme = uri_matches.str(1);

      // The "authority" fragment gets a bit more post-processing.
      L(FL("matched URI scheme: '%s'") % u.scheme);

      if (uri_matches[2].matched)
	{
	  string authority = uri_matches.str(2);
	  L(FL("matched URI authority: '%s'") % authority);

	  string user_part = "(?:([^@]+)@)?";
	  string ipv6_host_part = "\\[([^\\]]+)]\\]";
	  string normal_host_part = "([^:/]+)";
	  string host_part = "(?:" + ipv6_host_part + "|" + normal_host_part + ")";
	  string port_part = "(?::([[:digit:]]+))?";
	  string auth_rx = user_part + host_part + port_part;
	  boost::match_results<std::string::const_iterator> auth_matches;
	  I(boost::regex_match(authority, auth_matches, boost::regex(auth_rx)));
	  u.user = auth_matches.str(1);
	  u.port = auth_matches.str(4);
	  if (auth_matches[2].matched)	
	    u.host = auth_matches.str(2);
	  else
	    {
	      I(auth_matches[3].matched);
	      u.host = auth_matches.str(3);
	    }
	  L(FL("matched URI user: '%s'") % u.user);
	  L(FL("matched URI host: '%s'") % u.host);
	  L(FL("matched URI port: '%s'") % u.port);

	}

      u.path = uri_matches.str(3);
      u.query = uri_matches.str(4);
      u.fragment = uri_matches.str(5);
      L(FL("matched URI path: '%s'") % u.path);
      L(FL("matched URI query: '%s'") % u.query);
      L(FL("matched URI fragment: '%s'") % u.fragment);
      out = u;
      return true;
    }
  else
    return false;
}



#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"

static void
test_one_uri(string scheme,
	     string user,
	     string ipv6_host,
	     string normal_host,
	     string port,
	     string path,
	     string query,
	     string fragment)
{
  string built;

  if (!scheme.empty())
    built += scheme + ':';

  string host;

  if (! ipv6_host.empty())
    {
      I(normal_host.empty());
      host += '[';
      host += (ipv6_host + ']');
    }
  else
    host = normal_host;

  if (! (user.empty()
	 && host.empty()
	 && port.empty()))
    {
      built += "//";

      if (! user.empty())
	built += (user + '@');

      if (! host.empty())
	built += host;

      if (! port.empty())
	{
	  built += ':';
	  built += port;
	}
    }

  if (! path.empty())
    {
      I(path[0] == '/');
      built += path;
    }

  if (! query.empty())
    {
      built += '?';
      built += query;
    }

  if (! fragment.empty())
    {
      built += '#';
      built += fragment;
    }

  L(FL("testing parse of URI '%s'") % built);
  uri u;
  BOOST_CHECK(parse_uri(built, u));
  BOOST_CHECK(u.scheme == scheme);
  BOOST_CHECK(u.user == user);
  BOOST_CHECK(u.host == host);
  BOOST_CHECK(u.port == port);
  BOOST_CHECK(u.path == path);
  BOOST_CHECK(u.query == query);
  BOOST_CHECK(u.fragment == fragment);
}


static void
uri_test()
{
  test_one_uri("ssh", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "graydon", "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("file", "",       "", "",          "",   "/tmp/foo.mtn", "", "");
  test_one_uri("", "", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("http", "graydon", "", "venge.net", "8080", "/foo.cgi", "branch=foo", "tip");
}


void
add_uri_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&uri_test));
}

#endif // BUILD_UNIT_TESTS