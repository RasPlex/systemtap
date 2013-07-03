// -*- C++ -*-
// Copyright (C) 2012-2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
//
// ---
//
// This file incorporates code from the re2c project; please see
// the file README.stapregex for details.

#ifndef STAPREGEX_DFA_H
#define STAPREGEX_DFA_H

#include <string>
#include <iostream>

struct translator_output; /* from translator-output.h */

namespace stapregex {

struct regexp; /* from stapregex-tree.h */

struct dfa {
  unsigned nstates;
  unsigned ntags;

  void emit (translator_output *o) const;
  void emit_tagsave (translator_output *o, std::string tag_states,
                     std::string tag_vals, std::string tag_count) const;

  void print (translator_output *o) const;
  void print (std::ostream& o) const;
};

std::ostream& operator << (std::ostream &o, const dfa& d);

/* Produces a dfa that runs the specified code snippets based on match
   or fail outcomes for an unanchored (by default) match of re. */
dfa *stapregex_compile (regexp *re, const std::string& match_snippet, const std::string& fail_snippet);

};

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
