// tapset for per-method based probes
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
static const string TOK_MARK ("mark");
static const string TOK_JAVA ("java");

struct java_builder: public derived_probe_builder
{
private:
  bool cache_initialized;
  typedef multimap<string, string> java_cache_t;
  typedef multimap<string, string>::const_iterator java_cache_const_iterator_t;
  typedef pair<java_cache_const_iterator_t, java_cache_const_iterator_t>
    java_cache_const_iterator_pair_t;
  java_cache_t java_cache;

public:
  java_builder (): cache_initialized (false) {}

  void build (systemtap_session & sess,
	      probe * base,
	      probe_point * location,
	      literal_map_t const & parameters,
	      vector <derived_probe *> & finished_results);

  bool get_number_param (literal_map_t const & params,
			 string const & k, int & v);
  bool get_param (std::map<std::string, literal*> const & params,
		  const std::string& key,
		  std::string& value);
  void bminstall (systemtap_session & sess,
		 std::string java_proc);

};

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

void
java_builder::bminstall (systemtap_session & sess, std::string java_proc)
{
  std::vector<std::string> bminstall_cmd;
  bminstall_cmd.push_back(sess.bminstall_path);
  bminstall_cmd.push_back(java_proc);
  int ret = stap_system(sess.verbose, bminstall_cmd);
  if (sess.verbose > 2)
    {
      if (ret)
	clog << _F("WARNING: stap_system for bminstall.sh returned error: %d", ret) << endl;
      else
	clog << _F("stap_system for bminstall.sh returned: %d", ret) << endl;
    }
}

void
java_builder::build (systemtap_session & sess,
		     probe * base,
		     probe_point * loc,
		     literal_map_t const & parameters,
		     vector <derived_probe *> & finished_results)
{
  string method_str_val;
  bool has_method_str = get_param (parameters, TOK_METHOD, method_str_val);
  int short_method_pos = method_str_val.find ('(');
  //only if it exists, run check
  bool one_arg = false; // used to check if there is an argument in the method
  if (short_method_pos)
    {
      int second_method_pos = 0;
      second_method_pos = method_str_val.find (')');
      if ((second_method_pos - short_method_pos) >= 1)
	one_arg = true;
    }
  vector<int>::iterator it;
  vector<string>::iterator is;
  int _java_pid = 0;
  string _java_proc_class = "";
  string short_method_str = method_str_val.substr (0, short_method_pos);
  string class_str_val; // fully qualified class string
  bool has_class_str = get_param (parameters, TOK_CLASS, class_str_val);
  bool has_pid_int = get_number_param (parameters, TOK_JAVA, _java_pid);
  bool has_pid_str = get_param (parameters, TOK_JAVA, _java_proc_class);

  //need to count the number of parameters, exit if more than 10
  int method_params_counter = 1;
  int method_params_count = count (method_str_val.begin (), method_str_val.end (), ',');
  if (one_arg && method_params_count == 0)
    method_params_count++; // in this case we know there was at least a var, but no ','

  if (method_params_count > 10)
    {
      cerr << _("Error: Maximum of 10 method parameters may be specified") << endl;
      return;
    }
  assert (has_method_str);
  (void) has_method_str;
  assert (has_class_str);
  (void) has_class_str;
  string _tmp = "";
  if (! cache_initialized)
    {

      if(has_pid_int)
	{
	  _tmp = static_cast <ostringstream*> ( & (ostringstream ()
							 << (_java_pid)))->str ();

	  for (vector<string>::iterator iter = sess.byteman_script_path.begin();
	       iter != sess.byteman_script_path.end(); ++iter)
	    {
	      if (*iter == sess.tmpdir + "/stap-byteman-" + _tmp + ".btm")
		break;
	      else if ((*iter != sess.tmpdir + "/stap-byteman-" + _tmp + ".btm")
		       && ++iter == sess.byteman_script_path.end())
		sess.byteman_script_path.push_back(sess.tmpdir + "/stap-byteman-" + _tmp + ".btm");
	    }
	}
      else
	{
	  for (vector<string>::iterator iter = sess.byteman_script_path.begin();
	       iter != sess.byteman_script_path.end(); ++iter)
	    {
	      if (*iter == sess.tmpdir + "/stap-byteman-" + _java_proc_class + ".btm")
		break;
	      else if ((*iter != sess.tmpdir + "/stap-byteman-" + _java_proc_class + ".btm")
		       && ++iter == sess.byteman_script_path.end())
		sess.byteman_script_path.push_back(sess.tmpdir + "/stap-byteman-" + _java_proc_class + ".btm");
	    }
	}

      if (sess.byteman_script_path.size() == 0)
	sess.byteman_script_path.push_back(sess.tmpdir + "/stap-byteman-" + _java_proc_class + ".btm");

      if (sess.verbose > 3)
	{
	  //TRANSLATORS: the path to the byteman script
	  clog << _("byteman script path: ") << sess.byteman_script_path.at(*it)
	       << endl;
	}
      ofstream byteman_script;
      byteman_script.open ((sess.byteman_script_path.back()).c_str(), ifstream::app);
      if (! byteman_script)
	{
	  if (sess.verbose > 3)
	    //TRANSLATORS: Specific path cannot be opened
	    clog << sess.byteman_script_path.at(*it) << _(" cannot be opened: ")
		 << strerror (errno) << endl;
	  return;
	}

      if (sess.verbose > 2)
	clog << _("Writting byteman script") << endl;
      // none of this should be translated, byteman syntax is specific
      byteman_script << "RULE Stap " << sess.base_hash << method_str_val << endl;
      // we'll need detection here for second type of syntax
      // XXX change the method name being passed
      byteman_script << "CLASS " << class_str_val << endl
		     << "METHOD " << method_str_val << endl
		     << "HELPER HelperSDT" << endl
		     << "AT ENTRY" << endl
		     << "IF TRUE" << endl
		     << "DO METHOD_STAP_PROBE" << method_params_count
		     << "(\"" << class_str_val <<  "\", \""
		     << method_str_val << "\", ";
      // we need increment the var number, while decrementing the count
      for (method_params_counter = 1;
	   method_params_counter <= method_params_count;
	   method_params_counter++)
	{
	  byteman_script << "$" << method_params_counter;
	  if (! (method_params_counter + 1 >= method_params_count))
	    byteman_script << ", ";
	}
      byteman_script << ")" << endl;
      byteman_script << "ENDRULE" << endl;
      byteman_script.close();
      if (sess.verbose > 2)
	clog << _("Finished writting byteman script") << endl;
      
    }
#ifdef HAVE_HELPER
  /* we've written the byteman script, have its location 
   * re-write probe point to be process("libHelperSDTv2.so").mark("*")
   * use handler of first probe as body of second
   * exec bminstall PID
   * exec bmsubmit.sh -l script.btm
   * continue regular compilation and action with redefined probe
   */

  if (! (has_pid_int || has_pid_str) )
    exit (1); //XXX proper exit with warning message

  const char* java_pid_str;
  if (has_pid_int)
    java_pid_str = _tmp.c_str();
  else
    java_pid_str = _java_proc_class.c_str();
  
  sess.bminstall_path = (find_executable ("bminstall.sh"));

  if (sess.verbose > 2)
    clog << "Reported bminstall.sh path: " << sess.bminstall_path << endl;
  // XXX check both scripts here, exit if not available

  if (has_pid_int)
    {
      for (it = sess.java_pid.begin(); it != sess.java_pid.end(); ++it)
	{
	  if(*it == _java_pid) //if we work our way though it all and still doesn't 
	      break;           //match then we push back on the vector
	  else if ((*it != _java_pid) && ++it == sess.java_pid.end())
	    {
	      sess.java_pid.push_back(_java_pid);
	      bminstall(sess, java_pid_str);
	    }
	}
      if (sess.java_pid.size() == 0)
	{
	  sess.java_pid.push_back(_java_pid);
	  bminstall(sess, java_pid_str);
	}
    }
  else
    {
      for (is = sess.java_proc_class.begin(); is != sess.java_proc_class.end(); ++is)
	{
	  if(*is == _java_proc_class)
	      break;
	  else if ((*is != _java_proc_class) && (++is == sess.java_proc_class.end()))
	    {
	      sess.java_proc_class.push_back(_java_proc_class);
	      bminstall(sess, _java_proc_class);
	    }
	}
      if (sess.java_proc_class.size() == 0)
	{
	  sess.java_proc_class.push_back(_java_proc_class);
	  bminstall(sess, _java_proc_class);
	}
    }
  vector<string> bmsubmit_cmd;
  sess.bmsubmit_path = (find_executable ("bmsubmit.sh"));
  sess.byteman_log = sess.tmpdir + "/byteman.log";
  bmsubmit_cmd.push_back(sess.bmsubmit_path);
  bmsubmit_cmd.push_back(" -o");
  bmsubmit_cmd.push_back(sess.byteman_log);
  bmsubmit_cmd.push_back(" -l");
  bmsubmit_cmd.push_back(sess.byteman_script_path.back());

  (void) stap_system(sess.verbose, bmsubmit_cmd);
  if (sess.verbose > 3)
    clog << _("Reported bmsubmit.sh path: ") << sess.bmsubmit_path << endl;

  /* now we need to redefine the probe
   * while looking at sdt_query::convert_location as an example
   * create a new probe_point*, with same (*base_loc)
   * using a vector, iterate though, changing as needed
   * redefine functor values with new literal_string("foo")
   */
      //XXX can this be moved into its own function, or after root->bind'ing takes place
      probe_point* new_loc = new probe_point (*loc);
      vector<probe_point::component*> java_marker;
      java_marker.push_back( new probe_point::component 
	(TOK_PROCESS, new literal_string (HAVE_HELPER)));
      java_marker.push_back( new probe_point::component 
	(TOK_MARK, new literal_string ("*")));
      probe_point * derived_loc = new probe_point (*new_loc);

      block *b = new block;
      b->tok = base->body->tok;

      // first half of argument
      target_symbol *cc = new target_symbol;
      cc->tok = b->tok;
      cc->name = "$provider";

      functioncall *ccus = new functioncall;
      ccus->function = "user_string";
      ccus->type = pe_string;
      ccus->tok = b->tok;
      ccus->args.push_back(cc);
      
      // second half of argument
      target_symbol *mc = new target_symbol;
      mc->tok = b->tok;
      mc->name = "$name";

      functioncall *mcus = new functioncall;
      mcus->function = "user_string";
      mcus->type = pe_string;
      mcus->tok = b->tok;
      mcus->args.push_back(mc);
      
      //build if statement
      if_statement *ifs = new if_statement;
      ifs->thenblock = new next_statement;
      ifs->elseblock = NULL;
      ifs->tok = b->tok;
      ifs->thenblock->tok = b->tok;

      //class comparison
      comparison *ce = new comparison;
      ce->op = "!=";
      ce->tok = b->tok;
      ce->left = ccus;
      ce->right = new literal_string(class_str_val);
      ce->right->tok = b->tok;
      ifs->condition = ce;
      b->statements.push_back(ifs);

      //method comparision
      comparison *me = new comparison;
      me->op = "!=";
      me->tok = b->tok;
      me->left = mcus;
      me->right = new literal_string(method_str_val);
      me->right->tok = b->tok;

      logical_or_expr *le = new logical_or_expr;
      le->op = "||";
      le->tok = b->tok;
      le->left = ce;
      le->right = me;
      ifs->condition = le;
      b->statements.push_back(ifs);

      b->statements.push_back(base->body);
      base->body = b;

      derived_loc->components = java_marker;
      probe *new_mark_probe = base->create_alias (derived_loc, new_loc);
      derive_probes (sess, new_mark_probe, finished_results);
   
#else
  cerr << _("Cannot probe java method, configure --with-helper=") << endl;
#endif
}

void
register_tapset_java (systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new java_builder ();

  root->bind_str (TOK_JAVA)
    ->bind_str (TOK_CLASS)->bind_str (TOK_METHOD)
    ->bind(builder);

  root->bind_num (TOK_JAVA)
    ->bind_str (TOK_CLASS)->bind_str (TOK_METHOD)
    ->bind(builder);

}

