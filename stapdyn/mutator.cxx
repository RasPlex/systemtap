// stapdyn mutator functions
// Copyright (C) 2012-2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "mutator.h"

#include <algorithm>

extern "C" {
#include <dlfcn.h>
#include <wordexp.h>
#include <signal.h>
#include <time.h>
}

#include <BPatch_snippet.h>

#include "dynutil.h"
#include "../util.h"

extern "C" {
#include "../runtime/dyninst/stapdyn.h"
}

using namespace std;


// NB: since Dyninst callbacks have no context, we have to demux it
// to every mutator we've created, tracked by this vector.
static vector<mutator*> g_mutators;

static void
g_dynamic_library_callback(BPatch_thread *thread,
                           BPatch_module *module,
                           bool load)
{
  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->dynamic_library_callback(thread, module, load);
}


static void
g_post_fork_callback(BPatch_thread *parent, BPatch_thread *child)
{
  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->post_fork_callback(parent, child);
}


static void
g_exec_callback(BPatch_thread *thread)
{
  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->exec_callback(thread);
}


static void
g_exit_callback(BPatch_thread *thread, BPatch_exitType type)
{
  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->exit_callback(thread, type);
}


static void
g_thread_create_callback(BPatch_process *proc, BPatch_thread *thread)
{
  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->thread_create_callback(proc, thread);
}


static void
g_thread_destroy_callback(BPatch_process *proc, BPatch_thread *thread)
{
  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->thread_destroy_callback(proc, thread);
}


static pthread_t g_main_thread = pthread_self();
static const sigset_t *g_signal_mask;

static void
g_signal_handler(int signal)
{
  /* We only want the signal on our main thread, so it will interrupt the ppoll
   * loop.  If we get it on a different thread, just forward it.  */
  if (!pthread_equal(pthread_self(), g_main_thread))
    {
      pthread_kill(g_main_thread, signal);
      return;
    }

  for (size_t i = 0; i < g_mutators.size(); ++i)
    g_mutators[i]->signal_callback(signal);
}

__attribute__((constructor))
static void
setup_signals (void)
{
  struct sigaction sa;
  static sigset_t mask;
  static const int signals[] = {
      SIGHUP, SIGINT, SIGTERM, SIGQUIT,
  };

  /* Prepare the global sigmask for future use.  */
  sigemptyset (&mask);
  for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaddset (&mask, signals[i]);
  g_signal_mask = &mask;

  /* Prepare the common signal handler.  */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = g_signal_handler;
  sa.sa_flags = SA_RESTART;
  sigemptyset (&sa.sa_mask);
  for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaddset (&sa.sa_mask, signals[i]);

  /* Activate the handler for every signal.  */
  for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaction (signals[i], &sa, NULL);
}


mutator::mutator (const string& module_name,
                  vector<string>& module_options):
  module(NULL), module_name(resolve_path(module_name)),
  modoptions(module_options), p_target_created(false),
  p_target_error(false), utrace_enter_fn(NULL)
{
  // NB: dlopen does a library-path search if the filename doesn't have any
  // path components, which is why we use resolve_path(module_name)

  sigemptyset(&signals_received);

  g_mutators.push_back(this);
}

mutator::~mutator ()
{
  // Explicitly drop our mutatee references, so we better
  // control when their instrumentation is removed.
  target_mutatee.reset();
  mutatees.clear();

  if (module)
    {
      dlclose(module);
      module = NULL;
    }

  g_mutators.erase(find(g_mutators.begin(), g_mutators.end(), this));
}


// Do probes matching 'flag' exist?
bool
mutator::matching_probes_exist(uint64_t flag)
{
  for (size_t i = 0; i < targets.size(); ++i)
    {
      for (size_t j = 0; j < targets[i].probes.size(); ++j)
        {
	  if (targets[i].probes[j].flags & flag)
	    return true;
	}
    }
  return false;
}


// Load the stap module and initialize all probe info.
bool
mutator::load ()
{
  int rc;

  // Open the module directly, so we can query probes or run simple ones.
  (void)dlerror(); // clear previous errors
  module = dlopen(module_name.c_str(), RTLD_NOW);
  if (!module)
    {
      staperror() << "dlopen " << dlerror() << endl;
      return false;
    }

  if ((rc = find_dynprobes(module, targets)))
    return rc;
  if (!targets.empty())
    {
      // Always watch for new libraries to probe.
      patch.registerDynLibraryCallback(g_dynamic_library_callback);

      // Always watch for new child processes, even if we don't have
      // STAPDYN_PROBE_FLAG_PROC_BEGIN, because we might want to trigger
      // any of the other types of probes in new processes too.
      patch.registerPostForkCallback(g_post_fork_callback);
      patch.registerExecCallback(g_exec_callback);

      // Do we need a exit callback?
      if (matching_probes_exist(STAPDYN_PROBE_FLAG_PROC_END))
	patch.registerExitCallback(g_exit_callback);

      // Do we need a thread create callback?
      if (matching_probes_exist(STAPDYN_PROBE_FLAG_THREAD_BEGIN))
	patch.registerThreadEventCallback(BPatch_threadCreateEvent,
					  g_thread_create_callback);

      // Do we need a thread destroy callback?
      if (matching_probes_exist(STAPDYN_PROBE_FLAG_THREAD_END))
	patch.registerThreadEventCallback(BPatch_threadDestroyEvent,
					  g_thread_destroy_callback);
    }

  return true;
}

// Create a new process with the given command line
bool
mutator::create_process(const string& command)
{
  if (target_mutatee)
    {
      staperror() << "Already attached to a target process!" << endl;
      return false;
    }

  // Split the command into words.  If wordexp can't do it,
  // we'll just run via "sh -c" instead.
  const char** child_argv;
  const char* sh_argv[] = { "/bin/sh", "-c", command.c_str(), NULL };
  wordexp_t words;
  int rc = wordexp (command.c_str(), &words, WRDE_NOCMD|WRDE_UNDEF);
  if (rc == 0)
    child_argv = (/*cheater*/ const char**) words.we_wordv;
  else if (rc == WRDE_BADCHAR)
    child_argv = sh_argv;
  else
    {
      staperror() << "wordexp parsing error (" << rc << ")" << endl;
      return false;
    }

  // Search the PATH if necessary, then create the target process!
  string fullpath = find_executable(child_argv[0]);
  BPatch_process* app = patch.processCreate(fullpath.c_str(), child_argv);
  if (!app)
    {
      staperror() << "Couldn't create the target process" << endl;
      return false;
    }

  boost::shared_ptr<mutatee> m(new mutatee(app));
  mutatees.push_back(m);
  target_mutatee = m;
  p_target_created = true;

  if (!m->load_stap_dso(module_name))
    return false;

  if (!targets.empty())
    m->instrument_dynprobes(targets);

  return true;
}

// Attach to a specific existing process.
bool
mutator::attach_process(pid_t pid)
{
  if (target_mutatee)
    {
      staperror() << "Already attached to a target process!" << endl;
      return false;
    }

  BPatch_process* app = patch.processAttach(NULL, pid);
  if (!app)
    {
      staperror() << "Couldn't attach to the target process" << endl;
      return false;
    }

  boost::shared_ptr<mutatee> m(new mutatee(app));
  mutatees.push_back(m);
  target_mutatee = m;
  p_target_created = false;

  if (!m->load_stap_dso(module_name))
    return false;

  if (!targets.empty())
    m->instrument_dynprobes(targets);

  return true;
}

bool
mutator::init_modoptions()
{
  typeof(&stp_global_setter) global_setter = NULL;
  set_dlsym(global_setter, module, "stp_global_setter", false);

  if (global_setter == NULL)
    {
      // Hypothetical backwards compatibility with older stapdyn:
      stapwarn() << "Compiled module does not support -G globals" << endl;
      return false;
    }

  for (vector<string>::iterator it = modoptions.begin();
       it != modoptions.end(); it++)
    {
      string modoption = *it;

      // Parse modoption as "name=value"
      // XXX: compare whether this behaviour fits safety regex in buildrun.cxx
      string::size_type separator = modoption.find('=');
      if (separator == string::npos)
        {
          stapwarn() << "Could not parse module option '" << modoption << "'" << endl;
          return false; // XXX: perhaps ignore the option instead?
        }
      string name = modoption.substr(0, separator);
      string value = modoption.substr(separator+1);

      int rc = global_setter(name.c_str(), value.c_str());
      if (rc != 0)
        {
          stapwarn() << "Incorrect module option '" << modoption << "'" << endl;
          return false; // XXX: perhaps ignore the option instead?
        }
    }

  return true;
}

void
mutator::init_session_attributes()
{
  typeof(&stp_global_setter) global_setter = NULL;
  set_dlsym(global_setter, module, "stp_global_setter", false);

  if (global_setter == NULL)
    {
      // Just return.
      return;
    }

  // Note that the list of supported attributes should match with the
  // list in 'struct _stp_sesion_attributes' in
  // runtime/dyninst/session_attributes.h.

  int rc = global_setter("@log_level", lex_cast(stapdyn_log_level).c_str());
  if (rc != 0)
    stapwarn() << "Couldn't set 'log_level' global" << endl;

  rc = global_setter("@suppress_warnings",
		     lex_cast(stapdyn_suppress_warnings).c_str());
  if (rc != 0)
    stapwarn() << "Couldn't set 'suppress_warnings' global" << endl;

  rc = global_setter("@stp_pid", lex_cast(getpid()).c_str());
  if (rc != 0)
    stapwarn() << "Couldn't set 'stp_pid' global" << endl;

  if (target_mutatee)
    {
      rc = global_setter("@target", lex_cast(target_mutatee->process_id()).c_str());
      if (rc != 0)
        stapwarn() << "Couldn't set 'target' global" << endl;
    }

  size_t module_endpath = module_name.rfind('/');
  size_t module_basename_start =
    (module_endpath != string::npos) ? module_endpath + 1 : 0;
  size_t module_basename_end = module_name.find('.', module_basename_start);
  size_t module_basename_len = module_basename_end - module_basename_start;
  string module_basename(module_name, module_basename_start, module_basename_len);
  rc = global_setter("@module_name", module_basename.c_str());
  if (rc != 0)
    stapwarn() << "Couldn't set 'module_name' global" << endl;

  time_t now_t = time(NULL);
  struct tm* now = localtime(&now_t);
  if (now)
    {
      rc = global_setter("@tz_gmtoff", lex_cast(-now->tm_gmtoff).c_str());
      if (rc != 0)
        stapwarn() << "Couldn't set 'tz_gmtoff' global" << endl;
      rc = global_setter("@tz_name", now->tm_zone);
      if (rc != 0)
        stapwarn() << "Couldn't set 'tz_name' global" << endl;
    }
  else
    stapwarn() << "Couldn't discover local timezone info" << endl;

  if (stapdyn_outfile_name)
    {
      rc = global_setter("@outfile_name",
			 lex_cast(stapdyn_outfile_name).c_str());
      if (rc != 0)
	stapwarn() << "Couldn't set 'outfile_name' global" << endl;
    }

  return;
}

// Initialize the module session
bool
mutator::run_module_init()
{
  if (!module)
    return false;

  // First see if this is a shared-memory, multiprocess-capable module
  typeof(&stp_dyninst_shm_init) shm_init = NULL;
  typeof(&stp_dyninst_shm_connect) shm_connect = NULL;
  set_dlsym(shm_init, module, "stp_dyninst_shm_init", false);
  set_dlsym(shm_connect, module, "stp_dyninst_shm_connect", false);
  if (shm_init && shm_connect)
    {
      // Initialize the shared-memory locally.
      const char* shmem = shm_init();
      if (shmem == NULL)
        {
          stapwarn() << "stp_dyninst_shm_init failed!" << endl;
          return false;
        }
      module_shmem = shmem;
      // After the session is initilized, then we'll map shmem in the target
    }
  else if (target_mutatee)
    {
      // For modules that don't support shared-memory, but still have a target
      // process, we'll run init/exit in the target.
      target_mutatee->call_function("stp_dyninst_session_init");
      return true;
    }

  // From here, either this is a shared-memory module,
  // or we have no target and thus run init directly anyway.

  typeof(&stp_dyninst_session_init) session_init = NULL;
  try
    {
      set_dlsym(session_init, module, "stp_dyninst_session_init");
    }
  catch (runtime_error& e)
    {
      staperror() << e.what() << endl;
      return false;
    }

  // Before init runs, set any custom variables
  if (!modoptions.empty() && !init_modoptions())
    return false;

  init_session_attributes();

  int rc = session_init();
  if (rc)
    {
      stapwarn() << "stp_dyninst_session_init returned " << rc << endl;
      return false;
    }

  // Now we map the shared-memory into the target
  if (target_mutatee && !module_shmem.empty())
    {
      vector<BPatch_snippet *> args;
      args.push_back(new BPatch_constExpr(module_shmem.c_str()));
      target_mutatee->call_function("stp_dyninst_shm_connect", args);
    }

  return true;
}

// Shutdown the module session
bool
mutator::run_module_exit()
{
  if (!module)
    return false;

  if (target_mutatee && module_shmem.empty())
    {
      // For modules that don't support shared-memory, but still have a target
      // process, we'll run init/exit in the target.
      // XXX This may already have been done in its deconstructor if the process exited.
      target_mutatee->call_function("stp_dyninst_session_exit");
      return true;
    }

  // From here, either this is a shared-memory module,
  // or we have no target and thus run exit directly anyway.

  typeof(&stp_dyninst_session_exit) session_exit = NULL;
  try
    {
      set_dlsym(session_exit, module, "stp_dyninst_session_exit");
    }
  catch (runtime_error& e)
    {
      staperror() << e.what() << endl;
      return false;
    }

  session_exit();
  return true;
}


// Check the status of all mutatees
bool
mutator::update_mutatees()
{
  // We'll always break right away for SIGQUIT.  We'll also break for any other
  // signal if we didn't create the process.  Otherwise, we should give the
  // created process a chance to finish.
  if (sigismember(&signals_received, SIGQUIT) ||
      (!sigisemptyset(&signals_received) && !p_target_created))
    return false;

  if (target_mutatee && target_mutatee->is_terminated())
    return false;

  for (size_t i = 0; i < mutatees.size();)
    {
      boost::shared_ptr<mutatee> m = mutatees[i];
      if (m != target_mutatee && m->is_terminated())
        {
          mutatees.erase(mutatees.begin() + i);
          continue; // NB: without ++i
        }
      ++i;
    }

  return true;
}


// Start the actual systemtap session!
bool
mutator::run ()
{

  // Get the stap module ready...
  run_module_init();

  // And away we go!
  if (target_mutatee)
    {
      // For our first event, fire the target's process.begin probes (if any)
      target_mutatee->begin_callback();
      target_mutatee->continue_execution();

      // Dyninst's notification FD was fixed in 8.1; for earlier versions we'll
      // fall back to the fully-blocking wait for now.
#ifdef DYNINST_8_1
      // mask signals while we're preparing to poll
      stap_sigmasker masked(g_signal_mask);

      // Polling with a notification FD lets us wait on Dyninst while still
      // letting signals break us out of the loop.
      while (update_mutatees())
        {
          pollfd pfd;
          pfd.fd = patch.getNotificationFD();
          pfd.events = POLLIN;
          pfd.revents = 0;

          struct timespec timeout = { 10, 0 };

          int rc = ppoll (&pfd, 1, &timeout, &masked.old);
          if (rc < 0 && errno != EINTR)
            break;

          // Acknowledge and activate whatever events are waiting
          patch.pollForStatusChange();
        }
#else
      while (update_mutatees())
        patch.waitForStatusChange();
#endif
    }
  else // !target_mutatee
    {
      // With no mutatees, we just wait for a signal to exit.
      stap_sigmasker masked(g_signal_mask);
      while (sigisemptyset(&signals_received))
        sigsuspend(&masked.old);
    }

  // Indicate failure if the target had anything but EXIT_SUCCESS
  if (target_mutatee && target_mutatee->is_terminated())
    p_target_error = !target_mutatee->check_exit();

  // Detach from everything
  target_mutatee.reset();
  mutatees.clear();

  // Shutdown the stap module.
  return run_module_exit();
}


// Get the final exit status of this mutator
int mutator::exit_status ()
{
  if (!module)
    return EXIT_FAILURE;

  // NB: Only shm modules are new enough to have stp_dyninst_exit_status at
  // all, so we don't need to try in-target for old modules like session_exit.

  typeof(&stp_dyninst_exit_status) get_exit_status = NULL;
  set_dlsym(get_exit_status, module, "stp_dyninst_exit_status", false);
  if (get_exit_status)
    {
      int status = get_exit_status();
      if (status != EXIT_SUCCESS)
        return status;
    }

  return p_target_error ? EXIT_FAILURE : EXIT_SUCCESS;
}


// Find a mutatee which matches the given process, else return NULL
boost::shared_ptr<mutatee>
mutator::find_mutatee(BPatch_process* process)
{
  for (size_t i = 0; i < mutatees.size(); ++i)
    if (*mutatees[i] == process)
      return mutatees[i];
  return boost::shared_ptr<mutatee>();
}


// Callback to respond to dynamically loaded libraries.
// Check if it matches our targets, and instrument accordingly.
void
mutator::dynamic_library_callback(BPatch_thread *thread,
                                  BPatch_module *module,
                                  bool load)
{
  if (!load || !thread || !module)
    return;

  BPatch_process* process = thread->getProcess();
  staplog(1) << "dlopen \"" << module->libraryName()
             << "\", pid = " << process->getPid() << endl;
  boost::shared_ptr<mutatee> mut = find_mutatee(process);
  if (mut)
    mut->instrument_object_dynprobes(module->getObject(), targets);
}


// Callback to respond to post fork events.  Check if it matches our
// targets, and handle accordingly.
void
mutator::post_fork_callback(BPatch_thread *parent, BPatch_thread *child)
{
  if (!child || !parent)
    return;

  BPatch_process* child_process = child->getProcess();
  BPatch_process* parent_process = parent->getProcess();

  staplog(1) << "post fork, parent " << parent_process->getPid()
	     << ", child " << child_process->getPid() << endl;

  boost::shared_ptr<mutatee> mut = find_mutatee(parent_process);
  if (mut)
    {
      // Clone the mutatee for the new process.
      boost::shared_ptr<mutatee> m(new mutatee(child_process));
      mutatees.push_back(m);
      m->copy_forked_instrumentation(*mut);

      // Trigger any process.begin probes.
      m->begin_callback();
    }
}


// Callback to respond to exec events.  Check if it matches our
// targets, and handle accordingly.
void
mutator::exec_callback(BPatch_thread *thread)
{
  if (!thread)
    return;

  BPatch_process* process = thread->getProcess();

  staplog(1) << "exec, pid = " << process->getPid() << endl;

  boost::shared_ptr<mutatee> mut = find_mutatee(process);
  if (mut)
    {
      // Clear previous instrumentation
      mut->exec_reset_instrumentation();

      // FIXME the loadLibrary is hanging in Dyninst waiting for IRPC.
      // I've tried deferring this until update_mutatees() too - same hang.
#if 0
      // Load our module again in the new process
      if (mut->load_stap_dso(module_name))
        mut->instrument_dynprobes(targets, true);
#endif
    }
}


void
mutator::exit_callback(BPatch_thread *thread,
		       BPatch_exitType type __attribute__((unused)))
{
  if (!thread)
    return;

  // 'thread' is the thread that requested the exit, not necessarily the
  // main thread.
  BPatch_process* process = thread->getProcess();

  if (utrace_enter_fn == NULL)
    {
      try
        {
	  set_dlsym(utrace_enter_fn, module, "enter_dyninst_utrace_probe");
	}
      catch (runtime_error& e)
        {
	  staperror() << e.what() << endl;
	  return;
	}
    }

  staplog(1) << "exit callback, pid = " << process->getPid() << endl;

  boost::shared_ptr<mutatee> mut = find_mutatee(process);
  if (mut)
    {
      // FIXME: We'd like to call the mutatee's exit_callback()
      // function, but we've got a problem. The mutatee can't stop the
      // process to call the exit probe within the target (it finishes
      // exiting before we can). So, we'll call the probe(s) locally
      // here. This works, but the context is wrong (the mutator, not
      // the mutatee).
      vector<const dynprobe_location *> exit_probes;
      mut->find_attached_probes(STAPDYN_PROBE_FLAG_PROC_END, exit_probes);
      for (size_t p = 0; p < exit_probes.size(); ++p)
        {
	  const dynprobe_location *probe = exit_probes[p];
	  staplog(1) << "found end proc probe, index = " << probe->index
		     << endl;
	  int rc = utrace_enter_fn(probe->index, NULL);
	  if (rc)
	    stapwarn() << "enter_dyninst_utrace_probe returned "
		       << rc << endl;
	}
    }
}


void
mutator::thread_create_callback(BPatch_process *proc, BPatch_thread *thread)
{
  if (!proc || !thread)
    return;

  boost::shared_ptr<mutatee> mut = find_mutatee(proc);
  if (mut)
    mut->thread_callback(thread, true);
}


void
mutator::thread_destroy_callback(BPatch_process *proc, BPatch_thread *thread)
{
  if (!proc || !thread)
    return;

  boost::shared_ptr<mutatee> mut = find_mutatee(proc);
  if (mut)
    mut->thread_callback(thread, false);
}


// Callback to respond to signals.
void
mutator::signal_callback(int signal)
{
  sigaddset(&signals_received, signal);
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
