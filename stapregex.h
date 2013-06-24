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

#ifndef STAPREGEX_H
#define STAPREGEX_H

#include <string>
#include <iostream>
#include <stdexcept>

struct systemtap_session; /* from session.h */
struct token; /* from parse.h */
struct translator_output; /* from translate.h */

namespace stapregex {
  class regexp; /* from stapregex-tree.h */
  class dfa; /* from stapregex-dfa.h */
};

struct stapdfa {
  std::string orig_input;
  const token *tok;
  stapdfa (const std::string& func_name, const std::string& re,
           const token *tok = NULL, bool do_escape = true);
  ~stapdfa ();
  unsigned num_states();
  unsigned num_tags();
  void emit_declaration (translator_output *o);
  void emit_matchop_start (translator_output *o);
  void emit_matchop_end (translator_output *o);
  void print (std::ostream& o) const;
private:
  std::string func_name;
  stapregex::regexp *ast;
  stapregex::dfa *content;
};

std::ostream& operator << (std::ostream &o, const stapdfa& d);

/* Creates a dfa if no dfa for the corresponding regex exists yet;
   retrieves the corresponding dfa from s->dfas if already there: */
stapdfa *regex_to_stapdfa (systemtap_session *s, const std::string& input, const token* tok);

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
