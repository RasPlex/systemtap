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

#ifndef STAPREGEX_TREE_H
#define STAPREGEX_TREE_H

#include <string>
#include <deque>
#include <utility>
#include <stdexcept>

// XXX: currently we only support ASCII
#define NUM_REAL_CHARS 256

namespace stapregex {

typedef std::pair<unsigned, unsigned> segment;

struct range {
  std::deque<segment> segments;        // -- [lb, ub), sorted ascending

  range () {}                     // -- empty range
  range (char lb, char u);        // -- a segment [lb, u]
  range (const std::string& str); // -- character class (no named entities)
};

// NB: be sure to deallocate the old ranges if they are no longer used
range *range_union(range *a, range *b);
range *range_invert(range *ran);

// ------------------------------------------------------------------------

struct regexp {
  regexp () : num_tags(-1) {}
  virtual ~regexp ();
  int num_tags; // number of tag_op id's used in expression, -1 if unknown
  virtual const std::string type_of() = 0;
};

// ------------------------------------------------------------------------

struct null_op : public regexp {
  const std::string type_of() { return "null_op"; }
};

struct anchor_op : public regexp {
  char type;
  anchor_op (char type);
  const std::string type_of() { return "anchor_op"; }
};

struct tag_op : public regexp {
  unsigned id;
  tag_op (unsigned id);
  const std::string type_of() { return "tag_op"; }
};

struct match_op : public regexp {
  range *ran;
  match_op (range *ran);
  const std::string type_of() { return "match_op"; }
};

struct alt_op : public regexp {
  regexp *a, *b;
  alt_op (regexp *a, regexp *b);
  const std::string type_of() { return "alt_op"; }
};

struct cat_op : public regexp {
  regexp *a, *b;
  cat_op (regexp *a, regexp *b);
  const std::string type_of() { return "cat_op"; }
};

struct close_op : public regexp {
  regexp *re;
  close_op (regexp *re);
  const std::string type_of() { return "close_op"; }
};

struct closev_op : public regexp {
  regexp *re;
  int min, max; // -- use -1 to denote unboundedness in that direction
  closev_op (regexp *re, int min, int max);
  const std::string type_of() { return "closev_op"; }
};

// ------------------------------------------------------------------------

regexp *str_to_re(const std::string& str);

regexp *make_alt(regexp* a, regexp* b);
regexp *make_dot();

// ------------------------------------------------------------------------

struct regex_error: public std::runtime_error
{
  int pos; // -1 denotes error at unknown/indeterminate position
  regex_error (const std::string& msg):
    runtime_error(msg), pos(-1) {}
  regex_error (const std::string& msg, int pos):
    runtime_error(msg), pos(pos) {}
  ~regex_error () throw () {}
};

};

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
