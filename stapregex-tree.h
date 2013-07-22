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
#define NUM_REAL_CHARS 128

namespace stapregex {

typedef std::pair<char, char> segment;

struct range {
  std::deque<segment> segments;   // -- [lb, ub], sorted ascending // TODOXXX

  range () {}                     // -- empty range
  range (char lb, char ub);       // -- a segment [lb, ub]
  range (const std::string& str); // -- character class (no named entities)

  void print(std::ostream& o) const;
};

std::ostream& operator<< (std::ostream&, const range&);
std::ostream& operator<< (std::ostream&, const range*);

// NB: be sure to deallocate the old ranges if they are no longer used
range *range_union(range *a, range *b);
range *range_invert(range *ran);

// ------------------------------------------------------------------------

/* For the NFA representation, re2c uses an assembler-like notation,
   which should be easy enough to understand based on the meaning of
   FORK. An NFA is considered to be a tagged-NFA if it uses the TAG opcode.

   The only tricky thing here is instituting sensible defaults for
   tagged-NFA transition priorities. This is done by setting i->param = 0 
   on a FORK instruction if the FORK-target has lower priority, and
   i->param = 1 if it has higher priority. FORK is the only place
   where discriminating between transitions needs to be done. */

/* Opcodes for the assembly notation: */
const unsigned CHAR = 0;   // -- match character set (one successful outcome)
const unsigned GOTO = 1;
const unsigned FORK = 2;   // -- nondeterministic choice; param marks priority
const unsigned ACCEPT = 3; // -- final states; param marks success/fail
const unsigned TAG = 4;    // -- subexpression tracking; param marks tag #
const unsigned INIT = 5;   // -- opcode for ^ operator

/* To represent an NFA, allocate a continuous array of these ins units: */
union ins {
  struct {
    unsigned int tag:3;    // -- opcode
    unsigned int marked:1; // -- internal use; for algorithmic manipulation
    unsigned int param:8;  // -- numerical operand, e.g. tag #
    void *link;            // -- other instruction, e.g. FORK/GOTO target 
  } i;

  /* For the CHAR opcodes, we follow the instruction with a sequence of
     these special character-matching units, in ascending order: */
  struct {
    char value;            // -- character to match
    unsigned short bump;   // -- relative address of success-outcome insn
  } c;
};

inline bool marked(ins *i) { return i->i.marked != 0; }
inline void mark(ins *i) { i->i.marked = 1; }
inline void unmark(ins *i) { i->i.marked = 0; }

/* Helper function for printing out one ins element in a sequence: */
const ins* show_ins(std::ostream &o, const ins *i, const ins *base);

// ------------------------------------------------------------------------

struct regexp {
  int num_tags; // -- number of tag_op id's used in expression, -1 if unknown
  int size;     // -- number of instructions required for ins representation

  regexp () : num_tags(-1), size(-1) {}
  virtual ~regexp () {}

  virtual const std::string type_of() const = 0;

  /* Is regexp left-anchored? This function is used for optimization
     purposes, so it's always safe to return false. */
  virtual bool anchored() const { return false; }

  /* Length of array needed for ins array representation: */
  virtual void calc_size() = 0;
  unsigned ins_size() { if (size < 0) calc_size(); return size; }
  
  /* Compile to (part of) an already-allocated ins array: */
  virtual void compile(ins *i) = 0;

  /* Allocate a fresh ins array and compile: */
  ins *compile();

  /* Print out, with a careful eye as to bracketing:
       priority == 0 -- don't bracket anything
       priority == 1 -- bracket alt_op, but not cat_op
       priority == 2 -- bracket all compound operators */
  virtual void print(std::ostream& o, unsigned priority = 0) const = 0;
};

std::ostream& operator << (std::ostream &o, const regexp& re);
std::ostream& operator << (std::ostream &o, const regexp* re);

// ------------------------------------------------------------------------

struct null_op : public regexp {
  const std::string type_of() const { return "null_op"; }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    o << "{null}"; // XXX: pick a better pseudo-notation?
  }
};

struct anchor_op : public regexp {
  char type;
  anchor_op (char type);
  const std::string type_of() const { return "anchor_op"; }
  bool anchored () const { return type == '^'; }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    o << type;
  }  
};

struct tag_op : public regexp {
  unsigned id;
  tag_op (unsigned id);
  const std::string type_of() const { return "tag_op"; }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    o << "{t_" << id << "}";
  }
};

struct match_op : public regexp {
  range *ran;
  match_op (range *ran);
  const std::string type_of() const { return "match_op"; }
  void calc_size();
  void compile(ins *i);
  void print (std::ostream &o, unsigned priority) const { o << ran; }
};

struct alt_op : public regexp {
  regexp *a, *b;
  bool prefer_second;
  alt_op (regexp *a, regexp *b, bool prefer_second = false);
  const std::string type_of() const { return "alt_op"; }
  bool anchored () const { return a->anchored() && b->anchored(); }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    if (priority >= 1) o << "(";
    a->print(o, 0); o << "|"; b->print(o, 0);
    if (priority >= 1) o << ")";
  }
};

struct cat_op : public regexp {
  regexp *a, *b;
  cat_op (regexp *a, regexp *b);
  const std::string type_of() const { return "cat_op"; }
  bool anchored () const {
    return a->anchored(); // XXX: doesn't catch all cases, but that's all right
  }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    if (priority >= 2) o << "(";
    a->print(o, 1); b->print(o, 1);
    if (priority >= 2) o << ")";
  }
};

struct close_op : public regexp {
  regexp *re;
  bool prefer_shorter;
  close_op (regexp *re, bool prefer_shorter = false);
  const std::string type_of() const { return "close_op"; }
  bool anchored () const { return re->anchored(); }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    re->print(o, 2); o << "+";
  }
};

struct closev_op : public regexp {
  regexp *re;
  int nmin, nmax; // -- use -1 to denote unboundedness in that direction
  closev_op (regexp *re, int nmin, int nmax);
  const std::string type_of() const { return "closev_op"; }
  bool anchored () const { return nmin > 0 && re->anchored(); }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    re->print(o, 2); o << "{" << nmin << "," << nmax << "}";
  }
};

/* The following is somewhat generalized to allow implementing support
   for multiple distinct success outcomes, like in the original re2c: */
struct rule_op : public regexp {
  regexp *re;
  unsigned outcome; // -- 0 -> failure; 1 -> success; prefer success outcomes
  rule_op (regexp *re, unsigned outcome);
  const std::string type_of() const { return "rule_op"; }
  bool anchored () const { return re->anchored(); }
  void calc_size();
  void compile(ins *i);

  void print (std::ostream &o, unsigned priority) const {
    re->print(o, 1);
    if (outcome) o << "{success_" << outcome << "}";
    else o << "{failure_0}";
  }
};

// ------------------------------------------------------------------------

regexp *str_to_re(const std::string& str);

regexp *make_alt(regexp* a, regexp* b);
regexp *make_dot(bool allow_zero = false);

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
