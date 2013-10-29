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

#include "util.h"
#include "translator-output.h"

#include "session.h"
#include "staptree.h" // needed to use semantic_error

#include <iostream>
#include <cstdlib>
#include <string>
#include <algorithm>

using namespace std;

#include "stapregex-parse.h"
#include "stapregex-tree.h"
#include "stapregex-dfa.h"
#include "stapregex.h"

using namespace stapregex;

// ------------------------------------------------------------------------

stapdfa * 
regex_to_stapdfa (systemtap_session *s, const string& input, const token *tok)
{
  // Tags are disabled when not used, for the extra bit of efficiency.
  bool do_tag = s->need_tagged_dfa;

  if (s->dfas.find(input) != s->dfas.end())
    return s->dfas[input];

  stapdfa *dfa = new stapdfa ("__stp_dfa" + lex_cast(s->dfa_counter++), input, tok, true, do_tag);

  // Update required size of subexpression-tracking data structure:
  s->dfa_maxstate = max(s->dfa_maxstate, dfa->num_states());
  s->dfa_maxtag = max(s->dfa_maxtag, dfa->num_tags());

  s->dfas[input] = dfa;
  return dfa;
}

// ------------------------------------------------------------------------

stapdfa::stapdfa (const string& func_name, const string& re,
                  const token *tok, bool do_unescape, bool do_tag)
  : func_name(func_name), orig_input(re), tok(tok), do_tag(do_tag)
{
  try
    {
      regex_parser p(re, do_unescape);
      ast = p.parse (do_tag);
      content = stapregex_compile (ast, "goto match_success;", "goto match_fail;");
    }
  catch (const regex_error &e)
    {
      if (e.pos >= 0)
        throw SEMANTIC_ERROR(_F("regex compilation error (at position %d): %s",
                                e.pos, e.what()), tok);
      else
        throw SEMANTIC_ERROR(_F("regex compilation error: %s", e.what()), tok);
    }
}

stapdfa::~stapdfa ()
{
  delete content;
  delete ast;
}

unsigned
stapdfa::num_states () const
{
  return content->nstates;
}

unsigned
stapdfa::num_tags () const
{
  return content->ntags;
}

void
stapdfa::emit_declaration (translator_output *o) const
{
  o->newline() << "// DFA for \"" << orig_input << "\"";
  o->newline() << "int " << func_name << " (struct context * __restrict__ c, const char *str) {";
  o->indent(1);
  
  // Emit a SystemTap function body (as if an embedded-C snippet) that
  // invokes the DFA on the argument named 'str', saves a boolean
  // value (0 or 1) in the return value, and updates the probe
  // context's match object with subexpression contents as
  // appropriate.

  o->newline() << "const char *cur = str;";
  o->newline() << "const char *mar;";

  if (do_tag)
    o->newline() << "#define YYTAG(t,s,n) {c->last_match.tag_states[(t)][(s)] = (n);}";
  o->newline() << "#define YYCTYPE char";
  o->newline() << "#define YYCURSOR cur";
  o->newline() << "#define YYLIMIT cur";
  o->newline() << "#define YYMARKER mar";
  // XXX: YYFILL is disabled as it doesn't play well with ^
  o->newline();

  try
    {
      content->emit(o);
    }
  catch (const regex_error &e)
    {
      if (e.pos >= 0)
        throw SEMANTIC_ERROR(_F("regex compilation error (at position %d): %s",
                                e.pos, e.what()), tok);
      else
        throw SEMANTIC_ERROR(_F("regex compilation error: %s", e.what()), tok);
    }

  o->newline() << "#undef YYCTYPE";
  o->newline() << "#undef YYCURSOR";
  o->newline() << "#undef YYLIMIT";
  o->newline() << "#undef YYMARKER";

  o->newline() << "match_success:";
  if (do_tag)
    {
      o->newline() << "strlcpy (c->last_match.matched_str, str, MAXSTRINGLEN);";
      o->newline() << "c->last_match.result = 1";
      content->emit_tagsave(o, "c->last_match.tag_states", "c->last_match.tag_vals", "c->last_match.num_final_tags");
    }
  o->newline() << "return 1;";

  o->newline() << "match_fail:";
  if (do_tag)
    {  
      o->newline() << "strlcpy (c->last_match.matched_str, str, MAXSTRINGLEN);";
      o->newline() << "c->last_match.result = 0;";
    }
  o->newline() << "return 0;";

  o->newline(-1) << "}";
}

void
stapdfa::emit_matchop_start (translator_output *o) const
{
  o->line() << "(" << func_name << "(c, ("; // XXX: assumes context is available
}

void
stapdfa::emit_matchop_end (translator_output *o) const
{
  o->line() << ")))";
}

void
stapdfa::print (std::ostream& o) const
{
  translator_output to(o); print(&to);
}

void
stapdfa::print (translator_output *o) const
{
  o->line() << "STAPDFA (" << func_name << ", \"" << orig_input << "\") {";
  content->print(o);
  o->newline(-1) << "}";
}

std::ostream&
operator << (std::ostream &o, const stapdfa& d)
{
  d.print (o);
  return o;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
