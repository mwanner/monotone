// Copyright (C) 2009 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __AUTOMATE_OSTREAM_DEMUXED_HH__
#define __AUTOMATE_OSTREAM_DEMUXED_HH__

#include <iostream>

#include "automate_ostream.hh"

template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
class basic_automate_streambuf_demuxed : public std::basic_streambuf<_CharT, _Traits>
{
  typedef _Traits traits_type;
  typedef typename _Traits::int_type int_type;
  size_t _bufsize;
  std::basic_ostream<_CharT, _Traits> *stdout;
  std::basic_ostream<_CharT, _Traits> *errout;
  int err_code;
public:
  basic_automate_streambuf_demuxed(std::ostream & out, std::ostream & err,
                                   size_t bufsize) :
    std::streambuf(),
    _bufsize(bufsize),
    stdout(&out),
    errout(&err),
    err_code(0)
  {
    _CharT * inbuf = new _CharT[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }

  ~basic_automate_streambuf_demuxed() { }

  void set_err(int e)
  {
    err_code = e;
  }

  int get_error() const
  {
    return err_code;
  }

  void end_cmd()
  {
    _M_sync();
  }

  virtual int sync()
  {
    _M_sync();
    return 0;
  }

  int_type overflow(int_type c = traits_type::eof())
  {
    sync();
    sputc(c);
    return 0;
  }
private:
  void _M_sync()
  {
    std::basic_ostream<_CharT, _Traits> *str;
    str = ((err_code != 0) ? errout : stdout);
    if (!str)
      {
        setp(this->pbase(), this->pbase() + _bufsize);
        return;
      }
    int num = this->pptr() - this->pbase();
    if (num)
      {
        (*str) << std::basic_string<_CharT, _Traits>(this->pbase(), num);
        setp(this->pbase(), this->pbase() + _bufsize);
        str->flush();
      }
  }
};

template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
struct basic_automate_ostream_demuxed : public basic_automate_ostream<_CharT, _Traits>
{
  typedef basic_automate_streambuf_demuxed<_CharT, _Traits> streambuf_type;
  streambuf_type _M_autobuf;

  basic_automate_ostream_demuxed(std::basic_ostream<_CharT, _Traits> &out,
                                 std::basic_ostream<_CharT, _Traits> &err,
                                 size_t blocksize)
    : _M_autobuf(out, err, blocksize)
  { this->init(&_M_autobuf); }

  virtual ~basic_automate_ostream_demuxed()
  {}

  streambuf_type *
  rdbuf() const
  { return const_cast<streambuf_type *>(&_M_autobuf); }

  virtual void set_err(int e)
  { _M_autobuf.set_err(e); }

  int get_error() const
  { return _M_autobuf.get_error(); }

  virtual void end_cmd()
  { _M_autobuf.end_cmd(); }
};

typedef basic_automate_streambuf_demuxed<char> automate_streambuf_demuxed;
typedef basic_automate_ostream_demuxed<char> automate_ostream_demuxed;

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s: