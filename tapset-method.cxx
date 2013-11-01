// Tapset for per-method based probes
// Copyright (C) 2013 Red Hat Inc.

// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "session.h"
#include "tapsets.h"
#include "translate.h"
#include "util.h"
#include "config.h"

#include "unistd.h"
#include "sys/wait.h"
#include "sys/types.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

extern "C" {
#include <fnmatch.h>
}

using namespace std;
using namespace __gnu_cxx;

static const string TOK_CLASS ("class");
static const string TOK_METHOD ("method");
static const string TOK_PROCESS ("process");
static const string TOK_PROVIDER ("provider");
static const string TOK_MARK ("mark");
static const string TOK_JAVA ("java");
static const string TOK_RETURN ("return");
static const string TOK_BEGIN ("begin");
static const string TOK_END ("end");
static const string TOK_ERROR ("error");

// --------------------------------------------------------------------------

struct java_builder: public derived_probe_builder
{
private:
  typedef multimap<string, string> java_cache_t;
  typedef multimap<string, string>::const_iterator java_cache_const_iterator_t;
  typedef pair<java_cache_const_iterator_t, java_cache_const_iterator_t>
    java_cache_const_iterator_pair_t;
  java_cache_t java_cache;

public:
  java_builder () {}

  void build (systemtap_session & sess,
	      probe * base,
	      probe_point * location,
	      literal_map_t const & parameters,
	      vector <derived_probe *> & finished_results);

  bool has_null_param (literal_map_t const & params,
		       string const & k);

  bool get_number_param (literal_map_t const & params,
			 string const & k, int & v);
  bool get_param (std::map<std::string, literal*> const & params,
		  const std::string& key,
		  std::string& value);
  std::string mark_param(int i);

};

bool
java_builder::has_null_param(literal_map_t const & params,
			   string const & k)
{
  return derived_probe_builder::has_null_param(params, k);
}

bool
java_builder::get_number_param (literal_map_t const & params,
				string const & k, int & v)
{
  int64_t value;
  bool present = derived_probe_builder::get_param (params, k, value);
  v = (int) value;
  return present;
}

bool
java_builder::get_param (std::map<std::string, literal*> const & params,
                                  const std::string& key,
                                  std::string& value)
{
  map<string, literal *>::const_iterator i = params.find (key);
  if (i == params.end())
    return false;
  literal_string * ls = dynamic_cast<literal_string *>(i->second);
  if (!ls)
    return false;
  value = ls->value;
  return true;
}

std::string
java_builder::mark_param(int i)
{
  switch (i)
    {
    case 0:
      return "method__0";
    case 1:
      return "method__1";
    case 2:
      return "method__2";
    case 3:
      return "method__3";
    case 4:
      return "method__4";
    case 5:
      return "method__5";
    case 6:
      return "method__6";
    case 7:
      return "method__7";
    case 8:
      return "method__8";
    case 9:
      return "method__9";
    case 10:
      return "method__10";
    default:
      return "*";
    }
}

void
java_builder::build (systemtap_session & sess,
		     probe * base,
		     probe_point * loc,
		     literal_map_t const & parameters,
		     vector <derived_probe *> & finished_results)
{
  string method_str_val = "";
  string method_line_val = "";
  bool has_method_str = get_param (parameters, TOK_METHOD, method_str_val);
  int short_method_pos = method_str_val.find ('(');
  //only if it exists, run check
  bool one_arg = false; // used to check if there is an argument in the method
  if (short_method_pos)
    {
      int second_method_pos = 0;
      second_method_pos = method_str_val.find (')');
      if ((second_method_pos - short_method_pos) > 1)
	one_arg = true;
    }
  int _java_pid = 0;
  string _java_proc_class = "";
  string short_method_str = method_str_val.substr (0, short_method_pos);
  string class_str_val; // fully qualified class string
  bool has_class_str = get_param (parameters, TOK_CLASS, class_str_val);
  bool has_pid_int = get_number_param (parameters, TOK_JAVA, _java_pid);
  bool has_pid_str = get_param (parameters, TOK_JAVA, _java_proc_class);
  bool has_return = has_null_param (parameters, TOK_RETURN);
  bool has_line_number = false;

  //find if we're probing at a specific line number
  size_t line_position = 0;

  size_t method_end_pos = method_str_val.size();
  line_position = method_str_val.find_first_of(":"); //this will return the position ':' is found at
  if (line_position == string::npos)
    has_line_number = false;
  else
    {
      has_line_number = true;
      method_line_val = method_str_val.substr(line_position+1, method_end_pos);
      method_str_val = method_str_val.substr(0, line_position);
      line_position = method_line_val.find_first_of(":");
      if (line_position != string::npos)
        throw SEMANTIC_ERROR (_("maximum of one line number (:NNN)"));
      if (has_line_number && has_return)
        throw SEMANTIC_ERROR (_("conflict :NNN and .return probe"));
    }

  //need to count the number of parameters, exit if more than 10

  int method_params_count = count (method_str_val.begin (), method_str_val.end (), ',');
  if (one_arg)
    method_params_count++; // in this case we know there was at least a var, but no ','

  if (method_params_count > 10)
    throw SEMANTIC_ERROR (_("maximum of 10 java method parameters may be specified"));

  assert (has_method_str);
  (void) has_method_str;
  assert (has_class_str);
  (void) has_class_str;

  string java_pid_str = "";
  if(has_pid_int)
    java_pid_str = lex_cast(_java_pid);
  else 
    java_pid_str = _java_proc_class;

  if (! (has_pid_int || has_pid_str) )
    throw SEMANTIC_ERROR (_("missing JVMID"));

  /* The overall flow of control during a probed java method is something like this:

     (java) java-method -> 
     (java) byteman -> 
     (java) HelperSDT::METHOD_STAP_PROBENN ->
     (JNI) HelperSDT_arch.so -> 
     (C) sys/sdt.h marker STAP_PROBEN(hotspot,method__N,...,rulename)

     To catch the java-method hit that belongs to this very systemtap
     probe, we use the rulename string as the identifier.  It has to have
     some cool properties:
     - be unique system-wide, so as to avoid collisions between concurrent users, even if
       they run the same stap script
     - be unique within the script, so distinct probe handlers get run if specified
     - be computable from systemtap at run-time (since compile-time can't be unique enough)
     - be passable to stapbm, back through a .btm (byteman rule) file, back through sdt.h parameters

     The rulename is thusly synthesized as the string-concatenation expression 
            (module_name() . "probe_NNN")
  */

  string helper_location = PKGLIBDIR;  // probe process("$pkglibdir/libHelperSDT_*.so").provider("HelperSDT").mark("method_NNN")
  helper_location.append("/libHelperSDT_*.so"); // wildcard deliberate: want to catch all arches
  // probe_point* new_loc = new probe_point(*loc);
  vector<probe_point::component*> java_marker;
  java_marker.push_back(new probe_point::component 
                        (TOK_PROCESS, new literal_string (helper_location)));
  java_marker.push_back(new probe_point::component 
                        (TOK_PROVIDER, new literal_string ("HelperSDT")));
  java_marker.push_back(new probe_point::component 
                        (TOK_MARK, new literal_string (mark_param(method_params_count))));
  probe_point * derived_loc = new probe_point (java_marker);
  block *b = new block;
  b->tok = base->body->tok;

  functioncall* rulename_fcall = new functioncall; // module_name()
  rulename_fcall->tok = b->tok;
  rulename_fcall->function = "module_name";
  
  literal_string* rulename_suffix = new literal_string(base->name); // "probeNNNN"
  rulename_suffix->tok = b->tok;

  concatenation* rulename_expr = new concatenation; // module_name()."probeNN"
  rulename_expr->tok = b->tok;
  rulename_expr->left = rulename_fcall;
  rulename_expr->op = ".";
  rulename_expr->right = rulename_suffix;
  
  // the rulename arrives as sys/sdt.h $argN (for last arg)
  target_symbol *cc = new target_symbol; // $argN
  cc->tok = b->tok;
  cc->name = "$arg" + lex_cast(method_params_count+1);

  functioncall *ccus = new functioncall; // user_string($argN)
  ccus->function = "user_string";
  ccus->type = pe_string;
  ccus->tok = b->tok;
  ccus->args.push_back(cc);

  // rulename comparison:  (user_string($argN) != module_name()."probeNN")
  comparison *ce = new comparison;
  ce->op = "!=";
  ce->tok = b->tok;
  ce->left = ccus;
  ce->right = rulename_expr;
  ce->right->tok = b->tok;

  // build if statement: if (user_string($argN) != module_name()."probeNN") next;
  if_statement *ifs = new if_statement;
  ifs->thenblock = new next_statement;
  ifs->elseblock = NULL;
  ifs->tok = b->tok;
  ifs->thenblock->tok = b->tok;
  ifs->condition = ce;

  b->statements.push_back(ifs);
  b->statements.push_back(base->body);

  derived_loc->components = java_marker;
  probe* new_mark_probe = new probe (base, derived_loc);
  new_mark_probe->body = b;

  derive_probes (sess, new_mark_probe, finished_results);

  // the begin portion of the probe to install byteman rules in the target jvm

  vector<probe_point::component*> java_begin_marker;
  java_begin_marker.push_back( new probe_point::component (TOK_BEGIN));

  probe_point * der_begin_loc = new probe_point(java_begin_marker);

  /* stapbm takes the following arguments:
     $1 - install/uninstall
     $2 - JVM PID/unique name
     $3 - RULE name  <--- identifies this probe uniquely at run time
     $4 - class
     $5 - method
     $6 - number of args
     $7 - entry/exit/line
  */

  literal_string* leftbits =
    new literal_string(string(PKGLIBDIR)+string("/stapbm ") +
                       string("install ") + 
                       (has_pid_int ? 
                        lex_cast_qstring(java_pid_str) : 
                        lex_cast_qstring(_java_proc_class)) +
                       string(" "));
  // RULENAME_EXPR goes here
  literal_string* rightbits = 
    new literal_string(" " +
                       lex_cast_qstring(class_str_val) +
                       " " +
                       lex_cast_qstring(method_str_val) + 
                       " " +
                       lex_cast(method_params_count) +
                       " " +
                       ((!has_return && !has_line_number) ? string("entry") :
                        ((has_return && !has_line_number) ? string("exit") :
                         method_line_val)));

  concatenation* leftmid = new concatenation;
  leftmid->left = leftbits;
  leftmid->right = rulename_expr; // NB: we're reusing the same tree; 's ok due to copying
  leftmid->op = ".";
  leftmid->tok = base->body->tok;

  concatenation* midright = new concatenation;
  midright->left = leftmid;
  midright->right = rightbits;
  midright->op = ".";
  midright->tok = base->body->tok;

  block *bb = new block;
  bb->tok = base->body->tok;
  functioncall *fc = new functioncall;
  fc->function = "system";
  fc->tok = bb->tok;
  fc->args.push_back(midright);

  expr_statement* bs = new expr_statement;
  bs->tok = bb->tok;
  bs->value = fc;
  bb->statements.push_back(bs);

  der_begin_loc->components = java_begin_marker;
  probe* new_begin_probe = new probe(base, der_begin_loc);
  new_begin_probe->body = bb; // overwrite
  derive_probes (sess, new_begin_probe, finished_results);

  // the end/error portion of the probe to uninstall byteman rules from the target jvm

  vector<probe_point::component*> java_end_marker;
  java_end_marker.push_back(new probe_point::component (TOK_END));
  probe_point *der_end_loc = new probe_point (java_end_marker);

  vector<probe_point::component*> java_error_marker;
  java_error_marker.push_back(new probe_point::component (TOK_ERROR));
  probe_point *der_error_loc = new probe_point (java_error_marker);

  bb = new block;
  bb->tok = base->body->tok;

  leftbits =
    new literal_string(string(PKGLIBDIR)+string("/stapbm ") +
                       string("uninstall ") + 
                       (has_pid_int ? 
                        lex_cast_qstring(java_pid_str) : 
                        lex_cast_qstring(_java_proc_class)) +
                       string(" "));
  // RULENAME_EXPR goes here
  (void) rightbits; // same as before

  leftmid = new concatenation;
  leftmid->left = leftbits;
  leftmid->right = rulename_expr; // NB: we're reusing the same tree; 's ok due to copying
  leftmid->op = ".";
  leftmid->tok = bb->tok;

  midright = new concatenation;
  midright->left = leftmid;
  midright->right = rightbits;
  midright->op = ".";
  midright->tok = bb->tok;

  fc = new functioncall;
  fc->function = "system";
  fc->tok = bb->tok;
  fc->args.push_back(midright);

  bs = new expr_statement;
  bs->tok = bb->tok;
  bs->value = fc;

  bb->statements.push_back(bs);

  probe* new_end_probe = new probe(base, der_end_loc);
  new_end_probe->body = bb; // overwrite
  (void) der_error_loc;
  // new_end_probe->locations.push_back (der_error_loc);
  derive_probes (sess, new_end_probe, finished_results);
}

void
register_tapset_java (systemtap_session& s)
{
  (void) s;

#ifdef HAVE_JAVA
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new java_builder ();

  root->bind_str (TOK_JAVA)
    ->bind_str (TOK_CLASS)->bind_str (TOK_METHOD)
    ->bind_privilege(pr_all)
    ->bind(builder);

  root->bind_str (TOK_JAVA)
    ->bind_str (TOK_CLASS)->bind_str (TOK_METHOD)
    ->bind (TOK_RETURN)
    ->bind_privilege(pr_all)
    ->bind(builder);

  root->bind_num (TOK_JAVA)
    ->bind_str (TOK_CLASS)->bind_str (TOK_METHOD)
    ->bind_privilege(pr_all)
    ->bind (builder);

  root->bind_num (TOK_JAVA)
    ->bind_str (TOK_CLASS)->bind_str (TOK_METHOD)
    ->bind (TOK_RETURN)
    ->bind_privilege(pr_all)
    ->bind (builder);
#endif
}

