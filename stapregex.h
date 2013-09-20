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

struct systemtap_session; /* from session.h */
struct token; /* from parse.h */
class translator_output; /* from translator-output.h */

namespace stapregex {
  struct regexp; /* from stapregex-tree.h */
  struct dfa; /* from stapregex-dfa.h */
};

struct stapdfa {
  std::string func_name;
  std::string orig_input;
  const token *tok;

  stapdfa (const std::string& func_name, const std::string& re,
           const token *tok = NULL, bool do_unescape = true, bool do_tag = true);
  ~stapdfa ();
  unsigned num_states() const;
  unsigned num_tags() const;

  void emit_declaration (translator_output *o) const;
  void emit_matchop_start (translator_output *o) const;
  void emit_matchop_end (translator_output *o) const;

  void print(translator_output *o) const;
  void print(std::ostream& o) const;
private:
  stapregex::regexp *ast;
  stapregex::dfa *content;
  bool do_tag;
};

std::ostream& operator << (std::ostream &o, const stapdfa& d);

/* Creates a dfa if no dfa for the corresponding regex exists yet;
   retrieves the corresponding dfa from s->dfas if already there: */
stapdfa *regex_to_stapdfa (systemtap_session *s, const std::string& input, const token* tok);

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
