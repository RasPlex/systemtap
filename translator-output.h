// -*- C++ -*-
// Copyright (C) 2005, 2009, 2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef TRANSLATOR_OUTPUT_H
#define TRANSLATOR_OUTPUT_H

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

// Output context for systemtap translation, intended to allow
// pretty-printing.
class translator_output
{
  char *buf;
  std::ofstream* o2;
  std::ostream& o;
  unsigned tablevel;

public:
  std::string filename;

  translator_output (std::ostream& file);
  translator_output (const std::string& filename, size_t bufsize = 8192);
  ~translator_output ();

  std::ostream& newline (int indent = 0);
  void indent (int indent = 0);
  void assert_0_indent () { o << std::flush; assert (tablevel == 0); }
  std::ostream& line();

  std::ostream::pos_type tellp() { return o.tellp(); }
  std::ostream& seekp(std::ostream::pos_type p) { return o.seekp(p); }
};

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
