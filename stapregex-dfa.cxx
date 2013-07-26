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
#include <iostream>
#include <sstream>
#include <set>
#include <list>
#include <map>
#include <vector>
#include <queue>
#include <utility>

#include "translator-output.h"

#include "stapregex-parse.h"
#include "stapregex-tree.h"
#include "stapregex-dfa.h"

// Uncomment to show result of ins (NFA) compilation:
//#define STAPREGEX_DEBUG_INS
// Uncomment to display result of DFA compilation in a compact format:
//#define STAPREGEX_DEBUG_DFA
// Uncomment to have the generated engine do a trace of visited states:
//#define STAPREGEX_DEBUG_MATCH

using namespace std;

namespace stapregex {

regexp *pad_re = NULL;
regexp *fail_re = NULL;

dfa *
stapregex_compile (regexp *re, const std::string& match_snippet,
                   const std::string& fail_snippet)
{
  if (pad_re == NULL) {
    // build regexp for ".*"
    pad_re = make_dot ();
    pad_re = new close_op (pad_re, true); // -- prefer shorter match
    pad_re = new alt_op (pad_re, new null_op, true); // -- prefer second match
  }
  if (fail_re == NULL) {
    // build regexp for ".*$", but allow '\0' and support fail outcome
    fail_re = make_dot (true); // -- allow '\0'
    fail_re = new close_op (fail_re, true); // -- prefer shorter match
    fail_re = new alt_op (fail_re, new null_op, true); // -- prefer second match
    fail_re = new cat_op (fail_re, new anchor_op('$'));
    fail_re = new rule_op(fail_re, 0);
    // XXX: this approach creates one extra spurious-but-safe state
    // (safe because the matching procedure stops after encountering '\0')
  }

  vector<string> outcomes(2);
  outcomes[0] = fail_snippet;
  outcomes[1] = match_snippet;

  int num_tags = re->num_tags;

  // Pad & wrap re in appropriate rule_ops to control match behaviour:
  bool anchored = re->anchored ();
  if (!anchored) re = new cat_op(pad_re, re); // -- left-padding
  re = new rule_op(re, 1);
  re = new alt_op(re, fail_re);

#ifdef STAPREGEX_DEBUG_INS
  cerr << "RESULTING INS FROM REGEX " << re << ":" << endl;
#endif

  ins *i = re->compile();

#ifdef STAPREGEX_DEBUG_INS
  for (const ins *j = i; (j - i) < re->ins_size() + 1; )
    {
      j = show_ins(cerr, j, i); cerr << endl;
    }
  cerr << endl;
#endif
  
  // TODOXXX optimize ins as in re2c

  dfa *d = new dfa(i, num_tags, outcomes);

  // Carefully deallocate temporary scaffolding:
  if (!anchored) delete ((rule_op*) ((alt_op*) re)->a)->re; // -- new cat_op
  delete ((alt_op*) re)->a; // -- new rule_op
  delete re; // -- new alt_op
  // NB: deleting a regular expression DOES NOT deallocate its
  // children. The original re parameter is presumed to be retained
  // indefinitely as part of a stapdfa table, or such....

  return d;
}

// ------------------------------------------------------------------------

/* Now follows the heart of the tagged-DFA algorithm. This is a basic
   implementation of the algorithm described in Ville Laurikari's
   Masters thesis and summarized in the paper "NFAs with Tagged
   Transitions, their Conversion to Deterministic Automata and
   Application to Regular Expressions"
   (http://laurikari.net/ville/spire2000-tnfa.pdf).

   TODOXXX: The following does not contain a fully working
   implementation of the tagging support, but only of the regex
   matching.

   HERE BE DRAGONS (and not the friendly kind) */

/* Functions to deal with relative transition priorities: */

arc_priority
refine_higher(const arc_priority& a)
{
  return make_pair(2 * a.first + 1, a.second + 1);
}

arc_priority
refine_lower (const arc_priority& a)
{
  return make_pair(2 * a.first, a.second + 1);
}

int
arc_compare (const arc_priority& a, const arc_priority& b)
{
  unsigned long x = a.first;
  unsigned long y = b.first;

  if (a.second > b.second)
    x = x << (a.second - b.second);
  else if (a.second < b.second)
    y = y << (b.second - a.second);

  return ( x == y ? 0 : x < y ? -1 : 1 );
}

/* Manage the linked list of states in a DFA: */

state::state (state_kernel *kernel)
  : label(~0), next(NULL), kernel(kernel), accepts(false), accept_outcome(0) {}

state *
dfa::add_state (state *s)
{
  s->label = nstates++;
  
  if (last == NULL)
    {
      last = s;
      first = last;
    }
  else
    {
      // append to the end
      last->next = s;
      last = last->next;
    }

  return last;
}

/* Operations to build a simple kernel prior to taking closure: */

void
add_kernel (state_kernel *kernel, ins *i)
{
  kernel_point point;
  point.i = i;
  point.priority = make_pair(0,0);
  // NB: point->map_items is empty

  kernel->push_back(point);
}

state_kernel *
make_kernel (ins *i)
{
  state_kernel *kernel = new state_kernel;
  add_kernel (kernel, i);
  return kernel;
}

/* Compute the set of kernel_points that are 'tag-wise unambiguously
   reachable' from a given initial set of points. Absent tagging, this
   becomes a bog-standard NFA e_closure construction. */
state_kernel *
te_closure (state_kernel *start, int ntags, bool is_initial = false)
{
  state_kernel *closure = new state_kernel(*start);
  queue<kernel_point> worklist;

  /* To avoid searching through closure incessantly when retrieving
     information about existing elements, the following caches are
     needed: */
  vector<unsigned> max_tags (ntags, 0);
  map<ins *, list<kernel_point> > closure_map;

  /* Reset priorities and cache initial elements of closure: */
  for (state_kernel::iterator it = closure->begin();
       it != closure->end(); it++)
    {
      it->priority = make_pair(0,0);
      worklist.push(*it);

      // Store the element in relevant caches:

      for (list<map_item>::const_iterator jt = it->map_items.begin();
           jt != it->map_items.end(); jt++)
        max_tags[jt->first] = max(jt->second, max_tags[jt->first]);

      closure_map[it->i].push_back(*it);
    }

  while (!worklist.empty())
    {
      kernel_point point = worklist.front(); worklist.pop();

      // Identify e-transitions depending on the opcode.
      // There are at most two e-transitions emerging from an insn.
      // If we have two e-transitions, the 'other' has higher priority.

      ins *target = NULL; int tag = -1;
      ins *other_target = NULL; int other_tag = -1;

      // TODOXXX line-by-line proceeds below

      bool do_split = false;

      if (point.i->i.tag == TAG)
        {
          target = &point.i[1];
          tag = (int) point.i->i.param;
        }
      else if (point.i->i.tag == FORK && point.i == (ins *) point.i->i.link)
        {
          /* Workaround for a FORK that points to itself: */
          target = &point.i[1];
        }
      else if (point.i->i.tag == FORK)
        {
          do_split = true;
          // Relative priority of two e-transitions depends on param:
          if (point.i->i.param)
            {
              // Prefer jumping to link.
              target = &point.i[1];
              other_target = (ins *) point.i->i.link;
            }
          else
            {
              // Prefer stepping to next instruction.
              target = (ins *) point.i->i.link;
              other_target = &point.i[1];
            }
        }
      else if (point.i->i.tag == GOTO)
        {
          target = (ins *) point.i->i.link;
        }
      else if (point.i->i.tag == INIT && is_initial)
        {
          target = &point.i[1];
        }

      bool already_found;

      // Data for the endpoint of the first transition:
      kernel_point next;
      next.i = target;
      next.priority = do_split ? refine_lower(point.priority) : point.priority;
      next.map_items = point.map_items;

      // Date for the endpoint of the second transition:
      kernel_point other_next;
      other_next.i = other_target;
      other_next.priority = do_split ? refine_higher(point.priority) : point.priority;
      other_next.map_items = point.map_items;

      // Do infinite-loop-check:
      other_next.parents = point.parents;
      if (point.parents.find(other_next.i) != point.parents.end())
        {
          other_target = NULL;
          other_tag = -1;
        }
      other_next.parents.insert(other_next.i);

      next.parents = point.parents;
      if (point.parents.find(next.i) != point.parents.end())
        {
          target = NULL;
          tag = -1;
          goto next_target;
        }
      next.parents.insert(next.i);

    another_transition:
      if (target == NULL)
        continue;

      // Deal with the current e-transition:

      if (tag >= 0)
        {
          /* Delete all existing next.map_items of the form m[tag,x]. */
          for (list<map_item>::iterator it = next.map_items.begin();
               it != next.map_items.end(); )
            if (it->first == (unsigned) tag)
              {
                list<map_item>::iterator next_it = it;
                next_it++;
                next.map_items.erase (it);
                it = next_it;
              }
            else it++;

          /* Add m[tag,x] to next.map_items, where x is the smallest
             nonnegative integer such that m[tag,x] does not occur
             anywhere in closure. Then update the cache. */
          unsigned x = max_tags[tag];
          next.map_items.push_back(make_pair(tag, ++x));
          max_tags[tag] = x;
        }

      already_found = false;

      /* Deal with similar transitions that have a different priority. */
      for (list<kernel_point>::iterator it = closure_map[next.i].begin();
           it != closure_map[next.i].end(); )
        {
          int result = arc_compare(it->priority, next.priority);

          if (result > 0) {
            // obnoxious shuffle to avoid iterator invalidation
            list<kernel_point>::iterator old_it = it;
            it++;
            closure_map[next.i].erase(old_it);
            continue;
          } else if (result == 0) {
            already_found = true;
          }
          it++;
        }

      if (!already_found) {
        // Store the element in relevant caches:

        closure_map[next.i].push_back(next);

        for (list<map_item>::iterator jt = next.map_items.begin();
             jt != next.map_items.end(); jt++)
          max_tags[jt->first] = max(jt->second, max_tags[jt->first]);

        // Store the element in closure:
        closure->push_back(next);
        worklist.push(next);
      }

    next_target:
      // Now move to dealing with the second e-transition, if any.
      target = other_target; other_target = NULL;
      tag = other_tag; other_tag = -1;
      next = other_next;

      goto another_transition;
    }

  return closure;
}

/* Find the set of reordering commands (if any) that will get us from
   state s to some existing state in the dfa (returns the state in
   question, appends reordering commands to r). Returns NULL is no
   suitable state is found. */
state *
dfa::find_equivalent (state *s, tdfa_action &r)
{
  state *answer = NULL;

  for (state_kernel::iterator it = s->kernel->begin();
       it != s->kernel->end(); it++)
    mark(it->i);

  /* Check kernels of existing states for size equivalence and for
     unmarked items (similar to re2c's original algorithm): */
  unsigned n = s->kernel->size();
  for (state *t = first; t != NULL; t = t->next)
    {
      if (t->kernel->size() == n)
        {
          for (state_kernel::iterator it = t->kernel->begin();
               it != t->kernel->end(); it++)
              if (!marked(it->i)) 
                goto next_state;

          // TODOXXX check for existence of reordering r
          answer = t;
          goto cleanup;
        }
    next_state:
      ;
    }

 cleanup:
  for (state_kernel::iterator it = s->kernel->begin();
       it != s->kernel->end(); it++)
    unmark(it->i);

  return answer;
}


dfa::dfa (ins *i, int ntags, vector<string>& outcome_snippets)
  : orig_nfa(i), nstates(0), ntags(ntags), outcome_snippets(outcome_snippets)
{
  /* Initialize empty linked list of states: */
  first = last = NULL;

  ins *start = &i[0];
  state_kernel *seed_kernel = make_kernel(start);
  state_kernel *initial_kernel = te_closure(seed_kernel, ntags, true);
  delete seed_kernel;
  state *initial = add_state(new state(initial_kernel));
  queue<state *> worklist; worklist.push(initial);

  while (!worklist.empty())
    {
      state *curr = worklist.front(); worklist.pop();

      vector<list<ins *> > edges(NUM_REAL_CHARS);

      /* Using the CHAR instructions in kernel, build the initial
         table of spans for curr. Also check for final states. */

      for (list<kernel_point>::iterator it = curr->kernel->begin();
           it != curr->kernel->end(); it++)
        {
          if (it->i->i.tag == CHAR)
            {
              for (ins *j = &it->i[1]; j < (ins *) it->i->i.link; j++)
                edges[j->c.value].push_back((ins *) it->i->i.link);
            }
          else if (it->i->i.tag == ACCEPT)
            {
              /* Always prefer the highest numbered outcome: */
              curr->accepts = true;
              curr->accept_outcome = max(it->i->i.param, curr->accept_outcome);
            }
        }

      for (unsigned c = 0; c < NUM_REAL_CHARS; )
        {
          list <ins *> e = edges[c];
          assert (!e.empty()); // XXX: ensured by fail_re in stapregex_compile

          span s;

          s.lb = c;

          while (++c < NUM_REAL_CHARS && edges[c] == e) ;

          s.ub = c - 1;

          s.reach_pairs = new state_kernel;

          for (list<ins *>::iterator it = e.begin();
               it != e.end(); it++)
            add_kernel (s.reach_pairs, *it);

          curr->spans.push_back(s);
        }

      /* For each of the spans in curr, determine the reachable
         points assuming a character in the span. */
      for (list<span>::iterator it = curr->spans.begin();
           it != curr->spans.end(); it++)
        {
          state_kernel *reach_pairs = it->reach_pairs;

          /* Set up candidate target state: */
          state_kernel *u_pairs = te_closure(reach_pairs, ntags);
          state *target = new state(u_pairs);
          tdfa_action c;

          /* Generate position-save commands for any map items
             that do not appear in curr->kernel: */

          set<map_item> all_items;
          for (state_kernel::const_iterator jt = curr->kernel->begin();
               jt != curr->kernel->end(); jt++)
            for (list<map_item>::const_iterator kt = jt->map_items.begin();
                 kt != jt->map_items.end(); jt++)
              all_items.insert(*kt);

          list<map_item> store_items;
          for (state_kernel::const_iterator jt = u_pairs->begin();
               jt != u_pairs->end(); jt++)
            for (list<map_item>::const_iterator kt = jt->map_items.begin();
                kt != jt->map_items.end(); kt++)
              if (all_items.find(*kt) == all_items.end())
                store_items.push_back(*kt);

          for (list<map_item>::iterator jt = store_items.begin();
               jt != store_items.end(); jt++)
            {
              // append m[i,n] <- <curr position> to c
              tdfa_insn insn;
              insn.to = *jt;
              insn.save_pos = true;
              c.push_back(insn);
            }

          /* If there is a state t_prime in states such that some
             sequence of reordering commands r produces t_prime
             from target, use t_prime as the target state,
             appending the reordering commands to c. */
          state *t_prime = find_equivalent(target, c);
          if (t_prime != NULL)
            {
              delete target;
            }
          else
            {
              /* We need to actually add target to the dfa: */
              t_prime = target;
              add_state(t_prime);
              worklist.push(t_prime);

              if (t_prime->accepts)
                {
                  // TODOXXX set the finisher of t_prime
                }
            }

          /* Set the transition: */
          it->to = t_prime;
          it->action = c;
        }
    }
}

dfa::~dfa ()
{
  state * s;
  while ((s = first))
    {
      first = s->next;
      delete s;
    }

  delete orig_nfa;
}

// ------------------------------------------------------------------------

// TODOXXX add emission instructions for tag_ops

void
span::emit_jump (translator_output *o, const dfa *d) const
{
#ifdef STAPREGEX_DEBUG_MATCH
  o->newline () << "printf(\" --> GOTO yystate%d\\n\", " << to->label << ");";
#endif

  // TODOXXX tags feature allows proper longest-match priority
  if (to->accepts)
    {
      emit_final(o, d);
    }
  else
    {
      o->newline () << "YYCURSOR++;";
      o->newline () << "goto yystate" << to->label << ";";
    }
}

/* Assuming the target DFA of the span is a final state, emit code to
   (TODOXXX) cleanup tags and exit with a final answer. */
void
span::emit_final (translator_output *o, const dfa *d) const
{
  assert (to->accepts); // XXX: must guarantee correct usage of emit_final()
  o->newline() << d->outcome_snippets[to->accept_outcome];
  o->newline() << "goto yyfinish;";
}

string c_char(char c)
{
  stringstream o;
  o << "'";
  print_escaped(o, c);
  o << "'";
  return o.str();
}

void
state::emit (translator_output *o, const dfa *d) const
{
  o->newline() << "yystate" << label << ": ";
#ifdef STAPREGEX_DEBUG_MATCH
  o->newline () << "printf(\"READ '%s' %c\", cur, *YYCURSOR);";
#endif
  o->newline() << "switch (*YYCURSOR) {";
  o->indent(1);
  for (list<span>::const_iterator it = spans.begin();
       it != spans.end(); it++)
    {
      // If we see a '\0', go immediately into an accept state:
      if (it->lb == '\0')
        {
          o->newline() << "case " << c_char('\0') << ":";
          it->emit_final(o, d); // TODOXXX extra function may be unneeded
        }

      // Emit labels to handle all the other elements of the span:
      for (unsigned c = max('\1', it->lb); c <= (unsigned) it->ub; c++) {
        o->newline() << "case " << c_char((char) c) << ":";
      }
      it->emit_jump(o, d);

      // TODOXXX handle a 'default' set of characters for the largest span...
      // TODOXXX optimize by accepting before end of string whenever possible... (also necessary for proper first-matched-substring selection)
    }
  o->newline(-1) << "}";
}

void
dfa::emit (translator_output *o) const
{
#ifdef STAPREGEX_DEBUG_DFA
  print(o);
#else
  o->newline() << "{";
  o->newline(1);

  // XXX: workaround for empty regex
  if (first->accepts)
    {
      o->newline() << outcome_snippets[first->accept_outcome];
      o->newline() << "goto yyfinish;";      
    }

  for (state *s = first; s; s = s->next)
    s->emit(o, this);

  o->newline() << "yyfinish: ;";
  o->newline(-1) << "}";
#endif
}

void
dfa::emit_tagsave (translator_output *o, std::string tag_states,
                   std::string tag_vals, std::string tag_count) const
{
  // TODOXXX implement after testing the preceding algorithms
}

// ------------------------------------------------------------------------

std::ostream&
operator << (std::ostream &o, const tdfa_action& a)
{
  for (list<tdfa_insn>::const_iterator it = a.begin();
       it != a.end(); it++)
    {
      if (it != a.begin()) o << "; ";

      o << "m[" << it->to.first << "," << it->to.second << "] <- ";

      if (it->save_pos)
        o << "p";
      else
        o << "m[" << it->from.first << "," << it->from.second << "]";
    }

  return o;
}

std::ostream&
operator << (std::ostream &o, const arc_priority& p)
{
  o << p.first << "/" << (1 << p.second);
  return o;
}

void
state::print (translator_output *o) const
{
  o->line() << "state " << label;
  if (accepts)
    o->line() << " accepts " << accept_outcome;
  if (!finalizer.empty())
    o->line() << " [" << finalizer << "]";

  o->indent(1);
  for (list<span>::const_iterator it = spans.begin();
       it != spans.end(); it++)
    {
      o->newline() << "'";
      if (it->lb == it->ub)
        {
          print_escaped (o->line(), it->lb);
          o->line() << "  ";
        }
      else
        {
          print_escaped (o->line(), it->lb);
          o->line() << "-";
          print_escaped (o->line(), it->ub);
        }

      o->line() << "' -> " << it->to->label;

      if (!it->action.empty())
        o->line() << " [" << it->action << "]";
    }
  o->newline(-1);
}

void
dfa::print (std::ostream& o) const
{
  translator_output to(o); print(&to);
}

void
dfa::print (translator_output *o) const
{
  o->newline();
  for (state *s = first; s; s = s->next)
    {
      s->print(o);
      o->newline();
    }
  o->newline();
}

std::ostream&
operator << (std::ostream& o, const dfa& d)
{
  d.print(o);
  return o;
}

std::ostream&
operator << (std::ostream &o, const dfa *d)
{
  o << *d;
  return o;
}

};

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
