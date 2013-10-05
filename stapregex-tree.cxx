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
#include <iterator>
#include <algorithm>
#include <utility>
#include <cmath>
#include <cassert>

#include "stapregex-parse.h"
#include "stapregex-tree.h"

using namespace std;

namespace stapregex {

range::range (char lb, char ub)
{
  segments.push_back(make_pair(lb,ub));
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

void
range::print (std::ostream& o) const
{
  if (segments.empty())
    {
      o << "{none}"; // XXX: pick a better pseudo-notation?
      return;
    }

  if (segments.size() == 1 && segments[0].first == segments[0].second)
    {
      print_escaped (o, segments[0].first);
      return;
    }

  o << "[";
  for (deque<segment>::const_iterator it = segments.begin();
       it != segments.end(); it++)
    {
      char lb = it->first; char ub = it->second;
      if (lb == ub)
        {
          print_escaped (o, lb);
        }
      else
        {
          print_escaped (o, lb);
          o << "-";
          print_escaped (o, ub);
        }
    }
  o << "]";
}

std::ostream&
operator << (std::ostream& o, const range& ran)
{
  ran.print(o);
  return o;
}

std::ostream&
operator << (std::ostream& o, const range* ran)
{
  if (ran)
    o << *ran;
  else
    o << "{none}"; // XXX: pick a better pseudo-notation?
  return o;
}

// ------------------------------------------------------------------------

range *
range_union(range *old_a, range *old_b)
{
  if (old_a == NULL && old_b == NULL) return NULL;
  if (old_a == NULL) return new range(*old_b);
  if (old_b == NULL) return new range(*old_a);

  /* First, gather the segments from both ranges into one sorted pile: */
  deque<segment> s;
  merge(old_a->segments.begin(), old_a->segments.end(),
        old_b->segments.begin(), old_b->segments.end(),
        inserter(s, s.end()));

  /* Now go through and merge overlapping segments. */
  range *ran = new range;

  while (!s.empty())
    {
      /* Merge adjacent overlapping segments. */
      while (s.size() >= 2 && s[0].second >= s[1].first)
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

  char start = '\1'; // exclude '\0'

  while (!ran.segments.empty()) {
    char end = ran.segments.front().first - 1;
    if (start <= end) new_ran->segments.push_back(make_pair(start, end));
    start = ran.segments.front().second + 1;
    ran.segments.pop_front();
  }

  if ((unsigned)start < (unsigned)NUM_REAL_CHARS)
    new_ran->segments.push_back(make_pair(start, NUM_REAL_CHARS-1));

  return new_ran;
}

// ------------------------------------------------------------------------

const ins*
show_ins (std::ostream &o, const ins *i, const ins *base)
{
  o.width(3); o << (i - base) << ": ";

  const ins *ret = &i[1];

  switch (i->i.tag)
    {
    case CHAR:
      o << "match ";
      for (; ret < (ins *) i->i.link; ++ret) print_escaped(o, ret->c.value);
      break;

    case GOTO:
      o << "goto " << ((ins *) i->i.link - base);
      break;

    case FORK:
      o << "fork(" << ( i->i.param ? "prefer" : "avoid" ) << ") "
        << ((ins *) i->i.link - base);
      break;

    case ACCEPT:
      o << "accept(" << i->i.param << ")";
      break;

    case TAG:
      o << "tag(" << i->i.param << ")";
      break;

    case INIT:
      o << "init";
      break;
    }

  return ret;
}

// ------------------------------------------------------------------------

ins *
regexp::compile()
{
  unsigned k = ins_size();

  ins *i = new ins[k + 1];
  compile(i);
  
  // Append an infinite-loop GOTO to avoid edges going outside the array:
  i[k].i.tag = GOTO;
  i[k].i.link = &i[k];

  return i;
}

std::ostream&
operator << (std::ostream &o, const regexp& re)
{
  re.print (o);
  return o;
}

std::ostream&
operator << (std::ostream &o, const regexp* re)
{
  o << *re;
  return o;
}

// ------------------------------------------------------------------------

void
null_op::calc_size()
{
  size = 0;
}

void
null_op::compile(ins *i)
{
  ;
}

anchor_op::anchor_op(char type) : type(type) {}

void
anchor_op::calc_size()
{
  size = ( type == '^' ? 1 : 2 );
}

void
anchor_op::compile(ins *i)
{
  if (type == '^')
    {
      i->i.tag = INIT;
      i->i.link = &i[1];
    }
  else // type == '$'
    {
      i->i.tag = CHAR;
      i->i.link = &i[2];
      ins *j = &i[1];
      j->c.value = '\0';
      j->c.bump = 1;
    }
}

tag_op::tag_op(unsigned id) : id(id) {}

void
tag_op::calc_size()
{
  size = 1;
}

void
tag_op::compile(ins *i)
{
  i->i.tag = TAG;
  i->i.param = id;
}

match_op::match_op(range *ran) : ran(ran) {}

void
match_op::calc_size()
{
  size = 1;

  for (deque<segment>::iterator it = ran->segments.begin();
       it != ran->segments.end(); it++)
    {
      size += it->second - it->first + 1;
    }
}

void
match_op::compile(ins *i)
{
  unsigned bump = ins_size();
  i->i.tag = CHAR;
  i->i.link = &i[bump]; // mark end of table

  ins *j = &i[1];
  for (deque<segment>::iterator it = ran->segments.begin();
       it != ran->segments.end(); it++)
    {
      for (unsigned c = it->first; c <= (unsigned) it->second; c++)
        {
          j->c.value = c;
          j->c.bump = --bump; // mark end of table
          j++;
        }
    }
}

alt_op::alt_op(regexp *a, regexp *b, bool prefer_second)
  : a(a), b(b), prefer_second(prefer_second) {}

void
alt_op::calc_size()
{
  size = a->ins_size() + b->ins_size() + 2;
}

void
alt_op::compile(ins *i)
{
  i->i.tag = FORK;
  i->i.param = prefer_second ? 1 : 0; // preferred alternative to match
  ins *j = &i[a->ins_size() + 1];
  i->i.link = &j[1];
  a->compile(&i[1]);
  j->i.tag = GOTO;
  j->i.link = &j[b->ins_size() + 1];
  b->compile(&j[1]);
}

cat_op::cat_op(regexp *a, regexp *b) : a(a), b(b) {}

void
cat_op::calc_size()
{
  size = a->ins_size() + b->ins_size();
}

void
cat_op::compile(ins *i)
{
  a->compile(&i[0]);
  b->compile(&i[a->ins_size()]);
}

close_op::close_op(regexp *re, bool prefer_shorter)
  : re(re), prefer_shorter(prefer_shorter) {}

void
close_op::calc_size()
{
  size = re->ins_size() + 1;
}

void
close_op::compile(ins *i)
{
  re->compile(&i[0]);
  i += re->ins_size();
  i->i.tag = FORK;
  i->i.param = prefer_shorter ? 0 : 1; // XXX: match greedily by default
  i->i.link = i - re->ins_size();
}

closev_op::closev_op(regexp *re, int nmin, int nmax)
  : re(re), nmin(nmin), nmax(nmax) {}

void
closev_op::calc_size()
{
  unsigned k = re->ins_size();

  if (nmax >= 0)
    size = k * nmin + (nmax - nmin) * (1 + k);
  else
    size = k * nmin + 1;
}

void
closev_op::compile(ins *i)
{
  unsigned k = re->ins_size();

  ins *jumppoint = i + ((nmax - nmin) * (1 + k));

  for (int st = nmin; st < nmax; st++)
    {
      i->i.tag = FORK;
      i->i.param = 0; // XXX: this matches greedily
      i->i.link = jumppoint;
      i++;
      re->compile(&i[0]);
      i += k;
    }

  for (int st = 0; st < nmin; st++)
    {
      re->compile(&i[0]);
      i += k;

      if (nmax < 0 && st == 0)
        {
          i->i.tag = FORK;
          i->i.param = 1; // XXX: this matches greedily
          i->i.link = i - k;
          i++;
        }
    }
}

rule_op::rule_op(regexp *re, unsigned outcome) : re(re), outcome(outcome) {}

void
rule_op::calc_size()
{
  size = re->ins_size() + 1;
}

void
rule_op::compile(ins *i)
{
  re->compile(&i[0]);
  i += re->ins_size();
  i->i.tag = ACCEPT;
  i->i.param = outcome;
}

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
  else
    e1 = a;

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
  else
    e2 = b;

  range *u = range_union(r1, r2);
  delete r1; delete r2;

  match_op *m = u != NULL ? new match_op(u) : NULL;
  regexp *r = do_alt(m, do_alt(e1, e2));
  assert (r != NULL);
  return r;
}

regexp *
make_dot(bool allow_zero)
{
  return new match_op(new range(allow_zero ? 0 : 1, NUM_REAL_CHARS-1));
}

};

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
