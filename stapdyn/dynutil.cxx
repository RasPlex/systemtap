// stapdyn utility functions
// Copyright (C) 2012 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"

#include <iostream>
#include <string>
#include <cstdlib>

extern "C" {
#include <dlfcn.h>
#include <err.h>
#include <link.h>
}

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif

#include "dynutil.h"
#include "../util.h"

using namespace std;


// Callback for dl_iterate_phdr to look for libdyninstAPI.so
static int
guess_dyninst_rt_callback(struct dl_phdr_info *info,
                          size_t size __attribute__ ((unused)),
                          void *data)
{
  string& libdyninstAPI = *static_cast<string*>(data);

  const string name = info->dlpi_name ?: "(null)";
  if (name.find("libdyninstAPI.so") != string::npos)
    libdyninstAPI = name;

  return 0;
}

// Look for libdyninstAPI.so in our own process, and use that
// to guess the path for libdyninstAPI_RT.so
static const string
guess_dyninst_rt(void)
{
  string libdyninstAPI;
  dl_iterate_phdr(guess_dyninst_rt_callback, &libdyninstAPI);

  string libdyninstAPI_RT;
  size_t so = libdyninstAPI.rfind(".so");
  if (so != string::npos)
    {
      libdyninstAPI_RT = libdyninstAPI;
      libdyninstAPI_RT.insert(so, "_RT");
    }
  return libdyninstAPI_RT;
}

// Check that environment DYNINSTAPI_RT_LIB exists and is a valid file.
// If not, try to guess a good value and set it.
bool
check_dyninst_rt(void)
{
  static const char rt_env_name[] = "DYNINSTAPI_RT_LIB";
  const char* rt_env = getenv(rt_env_name);
  if (rt_env)
    {
      if (file_exists(rt_env))
        return true;
      staperror() << "Invalid " << rt_env_name << ": \"" << rt_env << "\"" << endl;
    }

  const string rt = guess_dyninst_rt();
  if (rt.empty() || !file_exists(rt))
    {
      staperror() << "Can't find libdyninstAPI_RT.so; try setting " << rt_env_name << endl;
      return false;
    }

  if (setenv(rt_env_name, rt.c_str(), 1) != 0)
    {
      int olderrno = errno;
      staperror() << "Can't set " << rt_env_name << ": " << strerror(olderrno);
      return false;
    }

  return true;
}


// Check that SELinux settings are ok for Dyninst operation.
bool
check_dyninst_sebools(bool attach)
{
#ifdef HAVE_SELINUX
  // For all these checks, we could examine errno on failure to act differently
  // for e.g. ENOENT vs. EPERM.  But since these are just early diagnostices,
  // I'm only going worry about successful bools for now.

  // deny_ptrace is definitely a blocker for us to attach at all
  if (security_get_boolean_active("deny_ptrace") > 0)
    {
      staperror() << "SELinux boolean 'deny_ptrace' is active, "
                       "which blocks Dyninst" << endl;
      return false;
    }

  // We might have to get more nuanced about allow_execstack, especially if
  // Dyninst is later enhanced to work around this restriction.  But for now,
  // this is also a blocker.
  if (security_get_boolean_active("allow_execstack") == 0)
    {
      staperror() << "SELinux boolean 'allow_execstack' is disabled, "
                       "which blocks Dyninst" << endl;
      return false;
    }

  // In process-attach mode, SELinux will trigger "avc:  denied  { execmod }"
  // on ld.so, when the mutator is injecting the dlopen for libdyninstAPI_RT.so.
  if (attach && security_get_boolean_active("allow_execmod") == 0)
    {
      staperror() << "SELinux boolean 'allow_execmod' is disabled, "
                       "which blocks Dyninst" << endl;
      return false;
    }
#else
  (void)attach; // unused
#endif

  return true;
}


// Check whether a process exited cleanly
bool
check_dyninst_exit(BPatch_process *process)
{
  int code;
  switch (process->terminationStatus())
    {
    case ExitedNormally:
      code = process->getExitCode();
      if (code == EXIT_SUCCESS)
        return true;
      stapwarn() << "Child process exited with status " << code << endl;
      return false;

    case ExitedViaSignal:
      code = process->getExitSignal();
      stapwarn() << "Child process exited with signal " << code
            << " (" << strsignal(code) << ")" << endl;
      return false;

    case NoExit:
      if (process->isTerminated())
        stapwarn() << "Child process exited in an unknown manner" << endl;
      else
        stapwarn() << "Child process has not exited" << endl;
      return false;

    default:
      return false;
    }
}


// Get an untyped symbol from a dlopened module.
// If flagged as 'required', throw an exception if missing or NULL.
void *
get_dlsym(void* handle, const char* symbol, bool required)
{
  const char* err = dlerror(); // clear previous errors
  void *pointer = dlsym(handle, symbol);
  if (required)
    {
      if ((err = dlerror()))
        throw std::runtime_error("dlsym " + std::string(err));
      if (pointer == NULL)
        throw std::runtime_error("dlsym " + std::string(symbol) + " is NULL");
    }
  return pointer;
}


//
// Logging, warnings, and errors, oh my!
//

// A null-sink output stream, similar to /dev/null
// (no buffer -> badbit -> quietly suppressed output)
static ostream nullstream(NULL);

// verbosity, increased by -v
unsigned stapdyn_log_level = 0;

// Whether to suppress warnings, set by -w
bool stapdyn_suppress_warnings = false;

// Output file name, set by -o
char *stapdyn_outfile_name = NULL;

// Return a stream for logging at the given verbosity level.
ostream&
staplog(unsigned level)
{
  if (level > stapdyn_log_level)
    return nullstream;
  return clog << program_invocation_short_name << ": ";
}

// Return a stream for warning messages.
ostream&
stapwarn(void)
{
  if (stapdyn_suppress_warnings)
    return nullstream;
  return clog << program_invocation_short_name << ": "
                   << colorize("WARNING:", "warning") << " ";
}

// Return a stream for error messages.
ostream&
staperror(void)
{
  return clog << program_invocation_short_name << ": "
                   << colorize("ERROR:", "error") << " ";
}

// Whether to color error and warning messages
bool color_errors; // Initialized in main()

// Adds coloring to strings
std::string
colorize(std::string str, std::string type)
{
  if (str.empty() || !color_errors)
    return str;
  else {
    // Check if this type is defined in SYSTEMTAP_COLORS
    std::string color = parse_stap_color(type);
    if (!color.empty()) // no need to pollute terminal if not necessary
      return "\033[" + color + "m\033[K" + str + "\033[m\033[K";
    else
      return str;
  }
}

/* Parse SYSTEMTAP_COLORS and returns the SGR parameter(s) for the given
type. The env var SYSTEMTAP_COLORS must be in the following format:
'key1=val1:key2=val2:' etc... where valid keys are 'error', 'warning',
'source', 'caret', 'token' and valid values constitute SGR parameter(s).
For example, the default setting would be:
'error=01;31:warning=00;33:source=00;34:caret=01:token=01'
*/
std::string
parse_stap_color(std::string type)
{
  const char *key, *col, *eq;
  int n = type.size();
  int done = 0;

  key = getenv("SYSTEMTAP_COLORS");
  if (key == NULL || *key == '\0')
    key = "error=01;31:warning=00;33:source=00;34:caret=01:token=01";

  while (!done) {
    if (!(col = strchr(key, ':'))) {
      col = strchr(key, '\0');
      done = 1;
    }
    if (!((eq = strchr(key, '=')) && eq < col))
      return ""; /* invalid syntax: no = in range */
    if (!(key < eq && eq < col-1))
      return ""; /* invalid syntax: key or val empty */
    if (strspn(eq+1, "0123456789;") < (size_t)(col-eq-1))
      return ""; /* invalid syntax: invalid char in val */
    if (eq-key == n && type.compare(0, n, key, n) == 0)
      return string(eq+1, col-eq-1);
    if (!done) key = col+1; /* advance to next key */
  }

  // Could not find the key
  return "";
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
