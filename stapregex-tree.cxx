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

#include <string>
#include <deque>
#include <utility>
#include <cmath>

#include "stapregex-parse.h"
#include "stapregex-tree.h"

using namespace std;

namespace stapregex {

range::range (char lb, char u)
{
  segments.push_back(make_pair(lb,u+1));
}

range::range (const string& str)
{
  cursor cur(&str); // no unescaping

  if (cur.finished) return;

  range *ran = stapregex_getrange(cur);

  while (!cur.finished)
    {
      range *add = stapregex_getrange(cur);
      range *new_ran = ( ran != NULL ? range_union(ran, add) : add );
      delete ran; if (new_ran != add) delete add;
      ran = new_ran;
    }

  segments = ran->segments;
  delete ran;
}

range *
range_union(range *old_a, range *old_b)
{
  if (old_a == NULL || old_a->segments.empty()) return new range(*old_b);
  if (old_b == NULL || old_b->segments.empty()) return new range(*old_a);

  range a(*old_a);
  range b(*old_b);

  /* First, gather the segments from both ranges into one sorted pile: */
  deque<segment> s;

  while (!a.segments.empty())
    {
      while (!b.segments.empty()
             && b.segments.front().first < a.segments.front().first)
        {
          s.push_back(b.segments.front()); b.segments.pop_front();
        }
      s.push_back(a.segments.front()); a.segments.pop_front();
    }
  while (!b.segments.empty())
    {
      s.push_back(b.segments.front()); b.segments.pop_front();
    }

  /* Now go through and merge overlapping segments. */
  range *ran = new range;

  while (!s.empty())
    {
      /* Merge adjacent overlapping segments. */
      while (s.size() >= 2 && s[0].second + 1 >= s[1].first)
        {
          segment merged = make_pair(min(s[0].first, s[1].first),
                                     max(s[0].second, s[1].second));
          s.pop_front(); s.pop_front();
          s.push_front(merged);
        } 

      /* Place non-overlapping segment in range. */
      ran->segments.push_back(s.front()); s.pop_front();
    }

  return ran;
}

range *
range_invert(range *old_ran)
{
  range ran(*old_ran);
  range *new_ran = new range;

  char start = 1;

  while (!ran.segments.empty()) {
    char end = ran.segments.front().first - 1;
    if (start <= end) new_ran->segments.push_back(make_pair(start, end));
    start = ran.segments.front().second;
    ran.segments.pop_front();
  }

  if (start <= NUM_REAL_CHARS)
    new_ran->segments.push_back(make_pair(start, NUM_REAL_CHARS));

  return new_ran;
}

// ------------------------------------------------------------------------

anchor_op::anchor_op(char type) : type(type) {}

tag_op::tag_op(unsigned id) : id(id) {}

match_op::match_op(range *ran) : ran(ran) {}

alt_op::alt_op(regexp *a, regexp *b) : a(a), b(b) {}

cat_op::cat_op(regexp *a, regexp *b) : a(a), b(b) {}

close_op::close_op(regexp *re) : re(re) {}

closev_op::closev_op(regexp *re, int min, int max) : re(re), min(min), max(max) {}

// ------------------------------------------------------------------------

regexp *
match_char(char c)
{
  return new match_op(new range(c,c));
}

regexp *
str_to_re(const string& str)
{
  if (str.empty()) return new null_op;

  regexp *re = match_char(str[0]);

  for (unsigned i = 1; i < str.length(); i++)
    re = new cat_op(re, match_char(str[i]));

  return re;
}

regexp *
do_alt(regexp *a, regexp *b)
{
  if (a == NULL) return b;
  if (b == NULL) return a;
  return new alt_op(a,b);
}

regexp *
make_alt(regexp *a, regexp *b)
{
  /* Optimize the case of building alternatives of match_op. */
  regexp *e1 = NULL, *e2 = NULL;
  range *r1 = NULL, *r2 = NULL;

  if (a->type_of() == "alt_op")
    {
      alt_op *aa = (alt_op *)a;
      if (aa->a->type_of() == "match_op")
        {
          r1 = ((match_op *) aa->a)->ran; e1 = aa->b;
        }
      else
        e1 = a;
    }
  else if (a->type_of() == "match_op")
    {
      r1 = ((match_op *) a)->ran; e1 = NULL;
    }

  if (b->type_of() == "alt_op")
    {
      alt_op *bb = (alt_op *)b;
      if (bb->a->type_of() == "match_op")
        {
          r2 = ((match_op *) bb->a)->ran; e2 = bb->b;
        }
      else
        e2 = b;
    }
  else if (b->type_of() == "match_op")
    {
      r2 = ((match_op *) b)->ran; e2 = NULL;
    }

  range *u = range_union(r1, r2);
  delete r1; delete r2;

  match_op *m = u != NULL ? new match_op(u) : NULL;
  regexp *r = do_alt(m, do_alt(e1, e2));
  return r;
}

regexp *
make_dot()
{
  return new match_op(new range(1, NUM_REAL_CHARS-1)); // exclude '\0'
}

};

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
