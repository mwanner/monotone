// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __VOCAB_CAST_HH
#define __VOCAB_CAST_HH

template<typename To, typename From>
To
typecast_vocab(From const & from)
{
  return To(from(), from.made_from);
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
