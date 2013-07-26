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

#include "stapregex-tree.h"
#include "stapregex-parse.h"

#include <cstdlib>
#include <cstring>
#include <string>

using namespace std;

namespace stapregex {

// TODOXXX compress / eliminate / move to util

// void prtChOrHex(std::ostream& o, unsigned c)
// {
// 	if (eFlag)
// 	{
// 		prtHex(o, c);
// 	}
// 	else if ((c < 256u) && (isprint(c) || isspace(c)))
// 	{
// 		prtCh(o, c);
// 	}
// 	else
// 	{
// 		prtHex(o, c);
// 	}
// }

// void prtHex(std::ostream& o, unsigned c)
// {
// 	int oc = (int)(c);

// 	if (re2c::uFlag)
// 	{
// 		o << "0x"
// 		  << hexCh(oc >> 28)
// 		  << hexCh(oc >> 24)
// 		  << hexCh(oc >> 20)
// 		  << hexCh(oc >> 16)
// 		  << hexCh(oc >> 12)
// 		  << hexCh(oc >>  8)
// 		  << hexCh(oc >>  4)
// 		  << hexCh(oc);
// 	}
// 	else if (re2c::wFlag)
// 	{
// 		o << "0x"
// 		  << hexCh(oc >> 12)
// 		  << hexCh(oc >>  8)
// 		  << hexCh(oc >>  4)
// 		  << hexCh(oc);
// 	}
// 	else
// 	{
// 		o << "0x"
// 		  << hexCh(oc >>  4) 
// 		  << hexCh(oc);
// 	}
// }

char octCh(unsigned c)
{
	return '0' + c % 8;
}

void prtCh(std::ostream& o, unsigned c)
{
	int oc = (int)(c);

	switch (oc)
	{
		case '\'':
		o << "\\'";
		break;

		case '"':
		o << "\\\"";
		break;

		case '\n':
		o << "\\n";
		break;

		case '\t':
		o << "\\t";
		break;

		case '\v':
		o << "\\v";
		break;

		case '\b':
		o << "\\b";
		break;

		case '\r':
		o << "\\r";
		break;

		case '\f':
		o << "\\f";
		break;

		case '\a':
		o << "\\a";
		break;

		case '\\':
		o << "\\\\";
		break;

		default:

		if ((oc < 256) && isprint(oc))
		{
			o << (char) oc;
		}
		else
		{
			o << '\\' << octCh(oc / 64) << octCh(oc / 8) << octCh(oc);
		}
	}
}

void print_escaped(std::ostream& o, char c)
{
  prtCh(o, c);
}

// ------------------------------------------------------------------------

cursor::cursor() : input(NULL), pos(~0),
                   last_pos(~0), next_c(0), last_c(0) {}

cursor::cursor(const std::string *input, bool do_unescape)
  : input(input), do_unescape(do_unescape), pos(0), last_pos(0)
{
  next_c = 0; last_c = 0;
  finished = ( pos >= input->length() );
}

char
cursor::next ()
{
  if (! next_c && finished)
    throw regex_error(_("unexpected end of regex"), pos);
  if (! next_c)
    get_unescaped();

  last_c = next_c;
  // advance by zeroing next_c
  next_c = 0;

  return last_c;
}

char
cursor::peek ()
{
  if (! next_c && ! finished)
      get_unescaped();

  // don't advance by zeroing next_c
  last_c = next_c;

  return next_c;
}

bool
cursor::has (unsigned n)
{
  return ( pos <= input->length() - n );
}

/* Systemtap doesn't unescape string literals for us, presuming to
   pass the backslashes intact to a C compiler; hence we need to do
   our own unescaping here.

   This functionality needs to be handled as part of cursor, in order
   to correctly retain the original positions in the string when doing
   error reporting. */
void
cursor::get_unescaped ()
{
  static const char *hex = "0123456789abcdef";
  static const char *oct = "01234567";

  last_pos = pos;
  char c = (*input)[pos];

  if (c != '\\' || !do_unescape)
    {
      next_c = c;
      pos++;
      finished = ( pos >= input->length() );
      return;
    }

  pos++;

  /* Check for improper string end: */
  if (pos >= input->length())
    throw regex_error(_("unexpected end of regex"), pos);

  /* The logic is based on re2c's Scanner::unescape() method;
     the set of accepted escape codes should correspond to
     lexer::scan() in parse.cxx. */
  c = (*input)[pos];
  switch (c)
    {
    case 'a': c = '\a'; break;
    case 'b': c = '\b'; break;
    case 't': c = '\t'; break;
    case 'n': c = '\n'; break;
    case 'v': c = '\v'; break;
    case 'f': c = '\f'; break;
    case 'r': c = '\r'; break;

    case 'x':
      {
      if (pos >= input->length() - 2)
        throw regex_error(_("two hex digits required in escape sequence"), pos);

      const char *d1 = strchr(hex, tolower((*input)[pos+1]));
      const char *d2 = strchr(hex, tolower((*input)[pos+2]));

      if (!d1 || !d2)
        throw regex_error(_("two hex digits required in escape sequence"), pos + (d1 ? 1 : 2));

      c = (char)((d1-hex) << 4) + (char)(d2-hex);
      pos += 2; // skip two chars more than usual
      break;
      }
    case '4' ... '7':
      // XXX: perhaps perform error recovery (slurp 3 octal chars)?
      throw regex_error(_("octal escape sequence out of range"), pos);

    case '0' ... '3':
      {
      if (pos >= input->length() - 2)
        throw regex_error(_("three octal digits required in escape sequence"), pos);

      const char *d0 = strchr(oct, (*input)[pos]);
      const char *d1 = strchr(oct, (*input)[pos+1]);
      const char *d2 = strchr(oct, (*input)[pos+2]);

      if (!d0 || !d1 || !d2)
        throw regex_error(_("three octal digits required in escape sequence"), pos + (d1 ? 1 : 2));
      
      c = (char)((d0-oct) << 6) + (char)((d1-oct) << 3) + (char)(d2-oct);
      pos += 2; // skip two chars more than usual
      break;
      }
    default:
      // do nothing; this removes the backslash from c
      ;
    }

  next_c = c;
  pos++;
  finished = ( pos >= input->length() );
}

// ------------------------------------------------------------------------

regexp *
regex_parser::parse (bool do_tag)
{
  cur = cursor(&input, do_unescape);
  num_tags = 0; this->do_tag = do_tag;

  regexp *result = parse_expr ();

  // PR15065 glom appropriate tag_ops onto the expr (subexpression 0)
  if (do_tag) {
    result = new cat_op(new tag_op(num_tags++), result);
    result = new cat_op(result, new tag_op(num_tags++));
  }

  if (! cur.finished)
    {
      char c = cur.peek ();
      if (c == ')')
        parse_error (_("unbalanced ')'"), cur.pos);
      else
        // This should not be possible:
        parse_error ("BUG -- regex parse failed to finish for unknown reasons", cur.pos);
    }

  // PR15065 store num_tags in result
  result->num_tags = num_tags;
  return result;
}

bool
regex_parser::isspecial (char c)
{
  return ( c == '.' || c == '[' || c == '{' || c == '(' || c == ')'
           || c == '\\' || c == '*' || c == '+' || c == '?' || c == '|'
           || c == '^' || c == '$' );
}

void
regex_parser::expect (char expected)
{
  char c = 0;
  try {
    c = cur.next ();
  } catch (const regex_error &e) {
    parse_error (_F("expected %c, found end of regex", expected));
  }

  if (c != expected)
    parse_error (_F("expected %c, found %c", expected, c));
}

void
regex_parser::parse_error (const string& msg, unsigned pos)
{
  throw regex_error(msg, pos);
}

void
regex_parser::parse_error (const string& msg)
{
  parse_error (msg, cur.last_pos);
}

// ------------------------------------------------------------------------

regexp *
regex_parser::parse_expr ()
{
  regexp *result = parse_term ();

  char c = cur.peek ();
  while (c && c == '|')
    {
      cur.next ();
      regexp *alt = parse_term ();
      result = make_alt (result, alt);
      c = cur.peek ();
    }

  return result;
}

regexp *
regex_parser::parse_term ()
{
  regexp *result = parse_factor ();

  char c = cur.peek ();
  while (c && c != '|' && c != ')')
    {
      regexp *next = parse_factor ();
      result = new cat_op(result, next);
      c = cur.peek ();
    }

  return result;
}

regexp *
regex_parser::parse_factor ()
{
  regexp *result;
  regexp *old_result = NULL;

  char c = cur.peek ();
  if (! c || c == '|' || c == ')')
    {
      result = new null_op;
      return result;
    }
  else if (c == '*' || c == '+' || c == '?' || c == '{')
    {
      parse_error(_F("unexpected '%c'", c));
    }

  if (isspecial (c) && c != '\\')
    cur.next (); // c is guaranteed to be swallowed

  if (c == '.')
    {
      result = make_dot ();
    }
  else if (c == '[')
    {
      result = parse_char_range ();
      expect (']');
    }
  else if (c == '(')
    {
      result = parse_expr ();

      // PR15065 glom appropriate tag_ops onto the expr
      if (do_tag) {
        result = new cat_op(new tag_op(num_tags++), result);
        result = new cat_op(result, new tag_op(num_tags++));
      } else {
        // XXX: workaround for certain error checking test cases which
        // would otherwise produce divergent behaviour
        // (e.g. "^*" vs "(^)*").
        result = new cat_op(result, new null_op);
      }

      expect (')');
    }
  else if (c == '^' || c == '$')
    {
      result = new anchor_op(c);
    }
  else // escaped or ordinary character -- not yet swallowed
    {
      string accumulate;
      char d = 0;

      while (c && ( ! isspecial (c) || c == '\\' ))
        {
          if (c == '\\')
            {
              cur.next ();
              c = cur.peek ();
            }

          cur.next ();
          d = cur.peek ();

          /* if we end in a closure, it should only govern the last character */
          if (d == '*' || d == '+' || d == '?' || d == '{')
            {
              /* save the last character */
              d = c; break;
            }

          accumulate.push_back (c);
          c = d; d = 0;
        }

      result = str_to_re (accumulate);

      /* separately deal with the last character before a closure */
      if (d != 0) {
        old_result = result; /* will add it back outside closure at the end */
        result = str_to_re (string(1,d));
      }
    }

  /* parse closures or other postfix operators */
  c = cur.peek ();
  while (c == '*' || c == '+' || c == '?' || c == '{')
    {
      cur.next ();

      /* closure-type operators applied to $^ are definitely not kosher */
      if (result->type_of() == "anchor_op")
        {
          parse_error(_F("postfix closure '%c' applied to anchoring operator", c));
        }

      if (c == '*')
        {
          result = make_alt (new close_op(result), new null_op);
        }
      else if (c == '+')
        {
          result = new close_op(result);
        }
      else if (c == '?')
        {
          result = make_alt (result, new null_op);
        }
      else if (c == '{')
        {
          int minsize = parse_number ();
          int maxsize = -1;

          c = cur.next ();
          if (c == ',')
            {
              c = cur.peek ();
              if (c == '}')
                {
                  cur.next ();
                  maxsize = -1;
                }
              else if (isdigit (c))
                {
                  maxsize = parse_number ();
                  expect ('}');
                }
              else
                parse_error(_("expected '}' or number"), cur.pos);
            }
          else if (c == '}')
            {
              maxsize = minsize;
            }
          else
            parse_error(_("expected ',' or '}'"));

          /* optimize {0,0}, {0,} and {1,} */
          if (!do_tag && minsize == 0 && maxsize == 0)
            {
              // XXX: this optimization is only used when
              // subexpression-extraction is disabled
              delete result;
              result = new null_op;
            }
          else if (minsize == 0 && maxsize == -1)
            {
              result = make_alt (new close_op(result), new null_op);
            }
          else if (minsize == 1 && maxsize == -1)
            {
              result = new close_op(result);
            }
          else
            {
              result = new closev_op(result, minsize, maxsize);
            }
        }
      
      c = cur.peek ();
    }

  if (old_result)
    result = new cat_op(old_result, result);

  return result;
}

regexp *
regex_parser::parse_char_range ()
{
  range *ran = NULL;

  // check for inversion
  bool inv = false;
  char c = cur.peek ();
  if (c == '^')
    {
      inv = true;
      cur.next ();
      c = cur.peek ();
    }

  for (;;)
    {
      // break on string end whenever we encounter it
      if (cur.finished) parse_error(_("unclosed character class")); // TODOXXX doublecheck that this is triggered correctly

      range *add = stapregex_getrange (cur);
      range *new_ran = ( ran != NULL ? range_union(ran, add) : add );
      delete ran; if (new_ran != add) delete add;
      ran = new_ran;

      // break on ']' (except at the start of the class)
      c = cur.peek ();
      if (c == ']')
        break;
    }

  if (inv)
    {
      range *new_ran = range_invert(ran);
      delete ran;
      ran = new_ran;
    }

  if (ran == NULL)
    return new null_op;

  return new match_op(ran);
}

unsigned
regex_parser::parse_number ()
{
  string digits;

  char c = cur.peek ();
  while (c && isdigit (c))
    {
      cur.next ();
      digits.push_back (c);
      c = cur.peek ();
    }

  if (digits == "") parse_error(_("expected number"), cur.pos);

  char *endptr = NULL;
  int val = strtol (digits.c_str (), &endptr, 10);

  if (*endptr != '\0' || errno == ERANGE) // paranoid error checking
    parse_error(_F("could not parse number %s", digits.c_str()), cur.pos);
#define MAX_DFA_REPETITIONS 12345
  if (val >= MAX_DFA_REPETITIONS) // XXX: is there a more sensible max size?
    parse_error(_F("%s is too large", digits.c_str()), cur.pos);

  return atoi (digits.c_str ());
}

// ------------------------------------------------------------------------

std::map<std::string, range *> named_char_classes;

range *
named_char_class (const string& name)
{
  // static initialization of table
  if (named_char_classes.empty())
    {
      // original source for these is http://www.regular-expressions.info/posixbrackets.html
      // also checked against (intended to match) the c stdlib isFOO() chr class functions
      named_char_classes["alpha"] = new range("A-Za-z");
      named_char_classes["alnum"] = new range("A-Za-z0-9");
      named_char_classes["blank"] = new range(" \t");
      named_char_classes["cntrl"] = new range("\x01-\x1F\x7F"); // XXX: include \x00 in range? -- probably not!
      named_char_classes["d"] = named_char_classes["digit"] = new range("0-9");
      named_char_classes["xdigit"] = new range("0-9a-fA-F");
      named_char_classes["graph"] = new range("\x21-\x7E");
      named_char_classes["l"] = named_char_classes["lower"] = new range("a-z");
      named_char_classes["print"] = new range("\x20-\x7E");
      named_char_classes["punct"] = new range("!\"#$%&'()*+,./:;<=>?@[\\]^_`{|}~-");
      named_char_classes["s"] = named_char_classes["space"] = new range(" \t\r\n\v\f");
      named_char_classes["u"] = named_char_classes["upper"] = new range("A-Z");
    }

  if (named_char_classes.find(name) == named_char_classes.end())
    {
      throw regex_error (_F("unknown character class '%s'", name.c_str())); // XXX: position unknown
    }

  return new range(*named_char_classes[name]);
}

range *
stapregex_getrange (cursor& cur)
{
  char c = cur.peek ();

  if (c == '\\')
    {
      // Grab escaped char regardless of what it is.
      cur.next (); c = cur.peek (); cur.next ();
    }
  else if (c == '[')
    {
      // Check for '[:' digraph.
      char old_c = c; cur.next (); c = cur.peek ();

      if (c == ':')
        {
          cur.next (); c = cur.peek (); // skip ':'
          string charclass;

          for (;;)
            {
              if (cur.finished)
                throw regex_error (_F("unclosed character class '[:%s'", charclass.c_str()), cur.pos);

              if (cur.has(2) && c == ':' && (*cur.input)[cur.pos] == ']')
                {
                  cur.next (); cur.next (); // skip ':]'
                  return named_char_class(charclass);
                }

              charclass.push_back(c); cur.next(); c = cur.peek();
            }
        }
      else
        {
          // Backtrack; fall through to processing c.
          c = old_c;
        }
    }
  else
    cur.next ();

  char lb = c, ub;

  if (!cur.has(2) || cur.peek () != '-' || (*cur.input)[cur.pos] == ']')
    {
      ub = lb;
    }
  else
    {
      cur.next (); // skip '-'
      ub = cur.peek ();

      if (ub < lb)
        throw regex_error (_F("Inverted character range %c-%c", lb, ub), cur.pos);

      cur.next ();
    }

  return new range(lb, ub);
}

};

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
