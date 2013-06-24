// -*- C++ -*-
// Copyright (C) 2005, 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "staptree.h"
#include "parse.h"
#include <string>


// ------------------------------------------------------------------------

#include "translator-output.h" // translator_output moved to separate file

// An unparser instance is in charge of emitting code for generic
// probe bodies, functions, globals.
struct unparser
{
  virtual ~unparser () {}

  virtual void emit_common_header () = 0;
  // #include<...>
  //
  // #define MAXNESTING nnn
  // #define MAXCONCURRENCY mmm
  // #define MAXSTRINGLEN ooo
  //
  // enum session_state_t {
  //   starting, begin, running, suspended, errored, ending, ended
  // };
  // static atomic_t session_state;
  //
  // struct context {
  //   unsigned errorcount;
  //   unsigned busy;
  //   ...
  // } context [MAXCONCURRENCY];

  // struct {
  virtual void emit_global (vardecl* v) = 0;
  // TYPE s_NAME;  // NAME is prefixed with "s_" to avoid kernel id collisions
  // rwlock_t s_NAME_lock;
  // } global = {
  virtual void emit_global_init (vardecl* v) = 0;
  // };

  // struct {
  virtual void emit_global_init_type (vardecl* v) = 0;
  // TYPE s_NAME;
  // } global_init = {
  //   ...
  // };  // variation of the above for dyninst static-initialization

  virtual void emit_global_param (vardecl* v) = 0; // for kernel
  // module_param_... -- at end of file

  virtual void emit_global_init_setters () = 0; // for dyninst
  // int stp_global_setter (const char *name, const char *value);
  // -- at end of file; returns -EINVAL on error

  virtual void emit_functionsig (functiondecl* v) = 0;
  // static void function_NAME (context* c);

  virtual void emit_kernel_module_init () = 0;
  virtual void emit_kernel_module_exit () = 0;
  // kernel module startup, shutdown

  virtual void emit_module_init () = 0;
  virtual void emit_module_refresh () = 0;
  virtual void emit_module_exit () = 0;
  // startup, probe refresh/activation, shutdown

  virtual void emit_function (functiondecl* v) = 0;
  // void function_NAME (struct context* c) {
  //   ....
  // }

  virtual void emit_probe (derived_probe* v) = 0;
  // void probe_NUMBER (struct context* c) {
  //   ... lifecycle
  //   ....
  // }
  // ... then call over to the derived_probe's emit_probe_entries() fn

  // The following helper functions must be used by any code-generation
  // infrastructure outside this file to properly mangle identifiers
  // appearing in the final code:
  virtual std::string c_localname (const std::string& e, bool mangle_oldstyle = false) = 0;
  virtual std::string c_globalname (const std::string &e) = 0;
  virtual std::string c_funcname (const std::string &e) = 0;
};


// Preparation done before checking the script cache, especially
// anything that might affect the hashed name.
int prepare_translate_pass (systemtap_session& s);

int translate_pass (systemtap_session& s);

#endif // TRANSLATE_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
