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

#include "../stapregex.h"
#include "../stapregex-tree.h"
#include "../translate.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>

using namespace std;

void print_usage(char *progname)
{
  fprintf (stderr, "usage: one of the following\n");
  fprintf (stderr, "$ %s 0 <regex> <string>\n", progname);
  fprintf (stderr, "$ %s 1 <regex> <string>\n", progname);
  fprintf (stderr, "$ %s 2 <regex> <string>\n", progname);
}

/* Quick-and-dirty thing to correct backslashes in C code: */
string escape_str(string n) {
  string foo("");
  for (string::iterator it = n.begin(); it != n.end(); it++) {
    if (*it == '\\') foo += "\\\\";
    else foo += *it;
  }
  return foo;
}

int main(int argc, char *argv [])
{
  if (argc < 2)
    {
      print_usage (argv[0]);
      exit (1);
    }

  int test_type = atoi (argv[1]);
    switch (test_type)
      {
      case 0:
      case 1:
      case 2:
        // glibc-style test syntax
        {
          if (argc != 4) { print_usage (argv[0]); exit (1); }
          string s(argv[2]);
          // generate a dfa with no unescaping and no tags
          stapdfa d("do_match", s, NULL, false, false);
          translator_output o(cout);

          string t(argv[3]);
          string match_expr = "\"" + escape_str(t) + "\"";
          
          // emit code skeleton
          o.line() << "// test output for systemtap-re2c";
          o.newline() << "#include <stdio.h>";
          o.newline() << "#include <stdlib.h>";
          o.newline() << "#include <string.h>";
          
          o.newline();
          d.emit_declaration (&o);
          /* NB: test type 2 will fail to compile, produce exception */
          o.newline();

          o.newline() << "int main()";
          o.newline() << "{";
          o.indent(1);
          o.newline() << "int ans = ";
          d.emit_matchop_start (&o);
          o.line() << match_expr;
          d.emit_matchop_end (&o);
          o.line() << ";";
          o.newline() << "printf(\"match %s\\n\", ans ? \"succeed\" : \"fail\");";
          if (test_type == 1) {
            o.newline() << "exit(ans ? 1 : 0);";
          } else if (test_type == 0) {
            o.newline() << "exit(ans ? 0 : 1);";
          }
          o.newline(-1) << "}";
          o.newline();
          
          break;
        }
      default:
        print_usage (argv[0]);
        exit (1);
      }
}
