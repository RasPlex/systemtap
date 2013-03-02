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

static const string TOK_CLASS("class");
static const string TOK_METHOD("method");
static const string TOK_PROCESS("process");
static const string TOK_MARK("mark");

/** check to see if TOK_CLASS is empty
   if so, reverse check TOK_METHOD for '.'
   representing the method name.
*/

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
  java_builder(): cache_initialized(false) {}

  void build(systemtap_session & sess,
	     probe * base,
	     probe_point * location,
	     literal_map_t const & parameters,
	     vector<derived_probe *> & finished_results);

  bool get_number_param(literal_map_t const & params,
			string const & k, int & v);
};

bool
java_builder::get_number_param(literal_map_t const & params,
			     string const & k, int & v)
{
  int64_t value;
  bool present = derived_probe_builder::get_param (params, k, value);
  v = (int) value;
  return present;
}


void
java_builder::build(systemtap_session & sess,
		    probe * base,
		    probe_point * loc,
		    literal_map_t const & parameters,
		    vector<derived_probe *> & finished_results)
{

  // XXX eventually this is where we'll check the type of format
  string method_str_val;
  bool has_method_str = get_param (parameters, TOK_METHOD, method_str_val);
  // XXX new var that is only the method name, go to the '('
  int short_method_pos = method_str_val.find('(');
  //only if it exists, run check
  string short_method_str = method_str_val.substr(0, short_method_pos);
  string class_str_val; // fully qualified class string
  bool has_class_str = get_param (parameters, TOK_CLASS, class_str_val);
  int java_pid;
  bool has_pid_int = get_number_param (parameters, TOK_PROCESS, java_pid);
  //need to count the number of parameters, exit if more than 10
  int method_params_counter = 1;
  // need to account for just one var (which won't have any ',')
  // XXX calculate distance between (), if greater than 0, assume one var
  int method_params_count = count(method_str_val.begin(), method_str_val.end(), ',');
  assert (has_method_str);
  (void) has_method_str;
  assert (has_class_str);
  (void) has_class_str;

  string byteman_script_path;
  if (! cache_initialized)
    {

      cache_initialized = true;
      byteman_script_path = sess.tmpdir + "/stap-byteman.btm";
      cout << "path: " << byteman_script_path << endl;
      ofstream byteman_script;
      byteman_script.open(byteman_script_path.c_str(), ifstream::out);
      if (! byteman_script)
	{
	  if (sess.verbose > 3)
	    //TRANSLATORS: Specific path cannot be opened
	    clog << byteman_script_path << _(" cannot be opened: ")
		 << strerror(errno) << endl;
	  return;
	}

      // none of this should be translated, byteman syntax is specific
      byteman_script << "RULE Stap " << sess.base_hash << endl;
      // we'll need detection here for second type of syntax
      // XXX change the method name being passed
      byteman_script << "CLASS " << class_str_val << endl
		     << "METHOD " << method_str_val << endl
		     << "HELPER HelperSDTv2" << endl
		     << "AT ENTRY" << endl
		     << "IF TRUE" << endl
		     << "DO METHOD_STAP_PROBE" << method_params_count
		     << "(\"stap-" << sess.base_hash << "\", \""
		     << short_method_str << "\", ";
      // we need increment the var number, while decrementing the count
      for(method_params_counter = 1;
	  method_params_counter <= method_params_count;
	  method_params_counter++)
	{
	  byteman_script << "$" << method_params_counter;
	  if(!(method_params_counter + 1 >= method_params_count))
	    byteman_script << ", ";
	}
      byteman_script << ")" << endl;
      byteman_script << "ENDRULE" << endl;
      byteman_script.close();
    }

  /* we've written the byteman script, have its location 
   * re-write probe point to be process("libHelperSDTv2.so").mark("*")
   * use handler of first probe as body of second
   * exec bminstall PID
   * exec bmsubmit.sh -l script.btm
   * continue regular compilation and action with redefined probe
   */

  if(!(has_pid_int))
    exit(1); //XXX proper exit with warning message

  //this could be done using itoa
  string arg = static_cast<ostringstream*>( &(ostringstream()
					      << java_pid) )->str();

  const char* a1 = arg.c_str();
  const char* b1 = (find_executable("bminstall.sh")).c_str();
  // XXX check both scripts here, exit if not available
  const char* space = " "; //XXX test if required

  int bminstall; //bminstall command status
  int bmsubmit; //bmsubmit command status
  pid_t install_pid = fork();
  if(install_pid == 0)
    {
      execl (b1, space, a1, (char*)NULL);
      _exit(EXIT_FAILURE);
    }
  else if (install_pid < 0) //failure
    bminstall = -1;
  else
    if (waitpid (install_pid, &bminstall, 0) != install_pid)
      bminstall = -1;
  //XXX better var names needed
  const char* a2 = " -l";
  const char* a3 = byteman_script_path.c_str();
  //XXX once again check if needed
  const char* b2 = (find_executable("bmsubmit.sh")).c_str();
  pid_t submit_pid = fork();
  if(submit_pid == 0)
    {
      execl(b2, a2, a3, NULL);
      _exit(EXIT_FAILURE);
    }
  else if (submit_pid < 0) //failure
    bmsubmit = -1;
  else
    if (waitpid (submit_pid, &bmsubmit, 0) != submit_pid)
      bmsubmit = -1;

  /* now we need to redefine the probe */
  /* while looking at sdt_query::convert_location as an example
   * create a new probe_point*, with same (*base_loc)
   * using a vector, iterate though, changing as needed
   * redefine functor values with new literal_string("foo")
   */
  probe_point* new_loc = new probe_point(*loc);
  
  //XXX this needs to be checked if it exists, and made a configure var
  //XXX rename the variables here
  string helper_loc = "/usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/jre/lib/amd64/libHelperSDTv2.so";
  vector<probe_point::component*> java_marker;
  java_marker.push_back( new probe_point::component(TOK_PROCESS, new literal_string(helper_loc)));
  java_marker.push_back( new probe_point::component(TOK_MARK, new literal_string("*")));
  probe_point * derived_loc = new probe_point(*new_loc);
  derived_loc->components = java_marker;
  probe *new_base = base->create_alias(derived_loc, new_loc);
  derive_probes(sess, new_base, finished_results);
}

void
register_tapset_java(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new java_builder(); //XXX need to define
  
  root = root->bind_num(TOK_PROCESS)
    ->bind_str(TOK_CLASS)->bind_str(TOK_METHOD);
  root->bind(builder);

}

