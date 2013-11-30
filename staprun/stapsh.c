// stapsh - systemtap remote shell
// Copyright (C) 2011 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
//

// stapsh implements a minimal protocol for a remote stap client to transfer a
// systemtap module to a temporary location and invoke staprun on it.  It is
// not meant to be invoked directly by the user.  Commands are simply
// whitespace-delimited strings, terminated by newlines.
//
//   command: stap VERSION
//     reply: stapsh VERSION MACHINE RELEASE
//      desc: This is the initial handshake.  The VERSION exchange is intended
//            to facilitate compatibility checks, in case the protocol needs to
//            change.  MACHINE and RELEASE are reported as given by uname.
//
//   command: option OPTIONNAME
//     reply: OK / error message
//      desc: Enables the option OPTIONNAME. Introduced in v2.4. Currently
//            supported options are:
//
//            data: Causes data output from staprun to be prefixed by a line
//            with the format "data stream size" where stream is either
//            "stdout" or "stderr" and size is the size of the data. It also
//            causes stapsh to send a "quit" message right before it leaves.
//            This is necessary in conditions where stap cannot detect the
//            state of stapsh from its pipes, such as when using a
//            virtio-serial port.
//
//            verbose: Increases verbosity of debug statements.
//
//   command: file SIZE NAME
//            DATA
//     reply: OK / error message
//      desc: Create a file of SIZE bytes, called NAME.  The NAME is a basename
//            only, and limited to roughly "[a-z0-9][a-z0-9._]*".  The DATA is
//            read as raw bytes following the command's newline.
//
//   command: run ARG1 ARG2 ...
//     reply: OK / error message
//      desc: Start staprun with the given quoted-printable arguments.  When
//            the child exits, stapsh will clean up and then exit with the same
//            return code.  Note that whitespace has significance in stapsh
//            command parsing, all tabs, spaces, and newlines must be escaped
//            in the arguments.  Any embedded NIL (=00) will truncate the
//            argument in the actual command invocation.
//
//   command: quit
//     reply: (none)
//      desc: Signal the child process to quit, then cleanup and exit.
//
// If stapsh reaches EOF on its standard input, it will send SIGHUP to the
// child process, wait for completion, then cleanup and exit normally.
// Since v2.4, the program can also be run in listening mode, compatible with
// QEMU's virtio-serial feature. In this mode, stapsh will listen for commands
// on the port. Behaviour is otherwise the same.


#include "../config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <poll.h>

#define STAPSH_TOK_DELIM " \t\r\n"
#define STAPSH_MAX_FILE_SIZE 32000000 // XXX should be cumulative?
#define STAPSH_MAX_ARGS 256


struct stapsh_handler {
    const char* name;
    int (*fn)(void);
};


static int do_hello(void);
static int do_option(void);
static int do_file(void);
static int do_run(void);
static int do_quit(void);

static const int signals[] = {
    SIGHUP, SIGPIPE, SIGINT, SIGTERM, SIGCHLD
};

static const struct stapsh_handler commands[] = {
      { "stap", do_hello },
      { "option", do_option },
      { "file", do_file },
      { "run", do_run },
      { "quit", do_quit },
};
static const unsigned ncommands = sizeof(commands) / sizeof(*commands);

static char tmpdir[FILENAME_MAX] = "";

static pid_t staprun_pid = -1;
static int fd_staprun_out = -1;
static int fd_staprun_err = -1;

struct pollfd pfds[3];
#define PFD_STAP_OUT    0
#define PFD_STAPRUN_OUT 1
#define PFD_STAPRUN_ERR 2

static unsigned prefix_data = 0;
static unsigned verbose = 0;

struct stapsh_option {
  const char* name;
  unsigned* var;
};

static const struct stapsh_option options[] = {
  { "verbose", &verbose },
  { "data", &prefix_data },
};
static const unsigned noptions = sizeof(options) / sizeof(*options);

// if != NULL, then we're in listening mode
static char *listening_port = NULL;
static int listening_port_fd = -1;

static struct utsname uts;

static FILE *stapsh_in, *stapsh_out, *stapsh_err;

static int sigio_enabled = 0;
static int sigio_port_status = 0;

// Returns nonzero if the host is connected. Used by any function/macro which
// writes to the port. This is necessary because if the host is not connected,
// a write() to the port will block.
static int host_connected()
{
  if (listening_port == NULL || stapsh_out == stdout)
    return 1;
  if (sigio_enabled)
    return sigio_port_status;
  // Note that POLLHUP is useless there, it's just so that events != 0
  struct pollfd p = { listening_port_fd, POLLHUP, 0 };
  if (poll(&p, 1, 0) < 0) // if we can't poll, we don't know the real
    return 0; // status, so be safe and say we're not connected
  else // rc >= 0   // POLLHUP occurred (or we timed out == connected)
    return !(p.revents & POLLHUP);
}

#define dbug(level, format, args...) do {                            \
  if (verbose >= level && host_connected())                          \
    fprintf (stapsh_err, "stapsh:%s:%d " format,                     \
              __FUNCTION__, __LINE__, ## args);                      \
  } while (0)

#define vdbug(level, format, args) do {                              \
  if (verbose >= level && host_connected()) {                        \
    fprintf (stapsh_err, "stapsh:%s:%d ", __FUNCTION__, __LINE__);   \
    vfprintf (stapsh_err, format, args);                             \
  } } while (0)

#define die(format, args...) do {                                    \
  if (host_connected()) {                                            \
    fprintf (stapsh_err, "stapsh:%s:%d " format,                     \
              __FUNCTION__, __LINE__, ## args);                      \
    fprintf (stapsh_err, ": %s (%d)\n", strerror(errno), errno); }   \
  cleanup(2); } while (0)



// Send a reply back to the client on stdout
static int __attribute__ ((format (printf, 1, 2)))
reply(const char* format, ...)
{
  if (!host_connected())
    return 1;
  va_list args, dbug_args;
  va_start (args, format);
  va_copy (dbug_args, args);
  vdbug (1, format, dbug_args);
  int ret = vfprintf (stapsh_out, format, args);
  fflush (stapsh_out);
  va_end (dbug_args);
  va_end (args);
  return ret;
}


static void __attribute__ ((noreturn))
cleanup(int status)
{
  // Mask signals, so if called from non-signal context, we
  // won't get a reentry (especially for SIGCHLD).
  unsigned i;
  sigset_t mask;
  sigemptyset (&mask);
  for (i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaddset (&mask, signals[i]);
  sigprocmask(SIG_BLOCK, &mask, 0);

  if (staprun_pid > 0)
    {
      int rc, ret;
      kill(staprun_pid, SIGHUP);
      ret = waitpid(staprun_pid, &rc, 0);
      if (status == 0)
        {
          if (ret == staprun_pid)
            status = WIFEXITED(rc) ? WEXITSTATUS(rc) : 128 + WTERMSIG(rc);
          else
            status = 2;
        }
    }

  if (tmpdir[0])
    {
      pid_t pid = 0;
      const char* argv[] = {"rm", "-rf", "--", tmpdir, NULL};
      if (chdir("/")) {} // ignore failure, rm will probably work anyway
      if (!posix_spawnp(&pid, argv[0], NULL, NULL,
                        (char* const*)argv, environ))
        waitpid(pid, NULL, 0);
    }

  // Announce we're quitting if prefixing is on. Schemes that use the "data"
  // option (e.g. unix and libvirt) are not able to detect when stapsh exits and
  // thus need this announcement to close the connection.
  if (prefix_data)
    reply("quit\n");

  if (listening_port)
    {
      fclose(stapsh_in);
      fclose(stapsh_out);
    }

  exit(status);
}

static void
handle_signal(int sig)
{
  dbug(1, "received signal %d: %s\n", sig, strsignal(sig));
  cleanup(0);
}

static void
sigio_handler(int sig)
{
  // sanity checks
  if (sig != SIGIO || listening_port_fd == -1)
    return;
  // Note that POLLHUP is useless there, it's just so that events != 0
  struct pollfd pfd = { listening_port_fd, POLLHUP, 0 };
  if (poll(&pfd, 1, 0) >= 0)
    // POLLHUP occurred (or we timed out == connected)
    sigio_port_status = !(pfd.revents & POLLHUP);
  // else (i.e. poll() failed), keep the same status
}

static int
can_sigio(void)
{
  return strverscmp("2.6.37", uts.release) <= 0;
}

static void
setup_signals (void)
{
  unsigned i;
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;
  sigemptyset (&sa.sa_mask);
  for (i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaddset (&sa.sa_mask, signals[i]);
  for (i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaction (signals[i], &sa, NULL);

  // catch SIGIO if kernel supports it
  if (listening_port && can_sigio())
    {
      memset(&sa, 0, sizeof(sa));
      sa.sa_handler = sigio_handler;
      sigemptyset (&sa.sa_mask);
      sigaddset (&sa.sa_mask, SIGIO);
      sigaction (SIGIO, &sa, NULL);
      sigio_enabled = 1;
    }
}

static void __attribute__ ((noreturn))
usage (char *prog, int status)
{
  fprintf (stapsh_err, "%s [-v] [-l PORT]\n", prog);
  exit (status);
}

static void
parse_args(int argc, char* const argv[])
{
  int c;
  while ((c = getopt (argc, argv, "vl:")) != -1)
    switch (c)
      {
      case 'v':
        ++verbose;
        break;
      case 'l':
        listening_port = optarg;
        break;
      case '?':
      default:
        usage (argv[0], 2);
      }
  if (optind < argc)
    {
      fprintf (stapsh_err, "%s: invalid extraneous arguments\n", argv[0]);
      usage (argv[0], 2);
    }
}


// Decode a quoted-printable string in-place
static int
qpdecode(char* s)
{
  char* o = s;
  while (*s)
    if (*s != '=')
      *o++ = *s++;
    else
      {
        if (s[1] == '\r' || s[1] == '\n')
          s += 2;
        else if (s[1] == '\r' && s[2] == '\n')
          s += 3;
        else if (!s[1] || !s[2])
          {
            dbug(2, "truncated quoted-printable escape \"%s\"\n", s);
            return 1;
          }
        else
          {
            errno = 0;
            char *end = 0, hex[] = { s[1], s[2], 0 };
            unsigned char c = strtol(hex, &end, 16);
            if (errno || end != hex + 2)
              {
                dbug(2, "invalid quoted-printable escape \"=%s\"\n", hex);
                return 1;
              }
            *o++ = c;
            s += 3;
          }
      }
  *o = '\0';
  return 0;
}


static int
do_hello()
{
  if (staprun_pid > 0)
    return 1;

  // XXX check caller's version compatibility

  reply ("stapsh %s %s %s\n", VERSION, uts.machine, uts.release);
  return 0;
}

static int
do_option()
{
  char *opt = strtok(NULL, STAPSH_TOK_DELIM);
  if (opt == NULL)
    return reply("ERROR: Could not parse option\n");

  // find and affect option
  unsigned i;
  for (i = 0; i < noptions; ++i)
    if (strcmp(opt, options[i].name) == 0)
      {
        dbug(2, "turning on option %s\n", opt);
        (*options[i].var)++;
        reply("OK\n");
        return 0;
      }
  return reply("ERROR: Invalid option\n");
}

static int
do_file()
{
  if (staprun_pid > 0)
    return 1;

  int ret = 0;
  int size = -1;
  const char* arg = strtok(NULL, STAPSH_TOK_DELIM);
  if (arg)
    size = atoi(arg);
  if (size <= 0 || size > STAPSH_MAX_FILE_SIZE)
    return reply ("ERROR: Bad file size %d\n", size);

  const char* name = strtok(NULL, STAPSH_TOK_DELIM);
  if (!name)
    return reply ("ERROR: Missing file name\n");
  for (arg = name; *arg; ++arg)
    if (!isalnum(*arg) &&
        !(arg > name && (*arg == '.' || *arg == '_')))
      return reply ("ERROR: Bad character '%c' in file name\n", *arg);

  FILE* f = fopen(name, "w");
  if (!f)
    return reply ("ERROR: Can't open file \"%s\" for writing\n", name);
  while (size > 0 && ret == 0)
    {
      char buf[1024];
      size_t r = sizeof(buf);
      if ((size_t)size < sizeof(buf))
	r = size;
      r = fread(buf, 1, r, stapsh_in);
      if (!r && feof(stapsh_in))
        ret = reply ("ERROR: Reached EOF while reading file data\n");
      else if (!r)
        ret = reply ("ERROR: Unable to read file data\n");
      else
        {
          size -= r;

          const char* bufp = buf;
          while (bufp < buf + r && ret == 0)
            {
              size_t w = (buf + r) - bufp;
              w = fwrite(bufp, 1, w, f);
              if (!w)
                ret = reply ("ERROR: Unable to write file data\n");
              else
                bufp += w;
            }
        }
    }
  fclose(f);

  if (ret == 0)
    reply ("OK\n");
  return ret;
}

// From util.cxx
static int
pipe_child_fd(posix_spawn_file_actions_t* fa, int pipefd[2], int childfd)
{
  if (pipe(pipefd))
    return -1;

  int err = 0;
  int dir = childfd ? 1 : 0;
  if (!fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) &&
      !fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) &&
      !(err = posix_spawn_file_actions_adddup2(fa, pipefd[dir], childfd)))
    return 0;

  int olderrno = errno;
  close(pipefd[0]);
  close(pipefd[1]);
  return err ?: olderrno;
}

// based on stap_spawn_piped from util.cxx
static pid_t
spawn_staprun_piped(char** args)
{
  pid_t pid = -1;
  int outfd[2], errfd[2];
  posix_spawn_file_actions_t fa;

  int err;
  if ((err = posix_spawn_file_actions_init(&fa)) != 0)
    {
      reply("ERROR: Can't initialize posix_spawn actions: %s\n", strerror(err));
      return -1;
    }
  if ((err = pipe_child_fd(&fa, outfd, 1)) != 0)
    {
      reply("ERROR: Can't create pipe for stdout: %s\n",
            err > 0 ? strerror(err) : "pipe_child_fd");
      goto cleanup_fa;
    }
  if ((err = pipe_child_fd(&fa, errfd, 2)) != 0)
    {
      reply("ERROR: Can't create pipe for stderr: %s\n",
            err > 0 ? strerror(err) : "pipe_child_fd");
      goto cleanup_out;
    }

  posix_spawn(&pid, args[0], &fa, NULL, args, environ);

  if (pid > 0)
    fd_staprun_err = errfd[0];
  else
    close(errfd[0]);
  close(errfd[1]);

cleanup_out:

  if (pid > 0)
    fd_staprun_out = outfd[0];
  else
    close(outfd[0]);
  close(outfd[1]);

cleanup_fa:

  posix_spawn_file_actions_destroy(&fa);
  return pid;
}

static pid_t
spawn_staprun(char** args)
{
  pid_t pid = -1;
  posix_spawn_file_actions_t fa;

  int err;
  if ((err = posix_spawn_file_actions_init(&fa)) != 0)
    {
      reply ("ERROR: Can't initialize posix_spawn actions: %s\n", strerror(err));
      return -1;
    }

  // no stdin for staprun
  if ((err = posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0)) != 0)
    {
      reply("ERROR: Can't set posix_spawn actions: %s\n", strerror(err));
      posix_spawn_file_actions_destroy(&fa);
      return -1;
    }

  if ((err = posix_spawn(&pid, args[0], &fa, NULL, args, environ)) != 0)
    {
      reply("ERROR: Can't launch staprun: %s\n", strerror(err));
      posix_spawn_file_actions_destroy(&fa);
      return -1;
    }

  posix_spawn_file_actions_destroy(&fa);
  return pid;
}

static int
do_run()
{
  if (staprun_pid > 0)
    return 1;

  char staprun[] = BINDIR "/staprun";
  char* args[STAPSH_MAX_ARGS + 1] = { staprun, 0 };
  unsigned nargs = 1;

  char* arg;
  while ((arg = strtok(NULL, STAPSH_TOK_DELIM)))
    {
      // check if we have space left
      // note that we have to keep at least one 0 at the end for posix_spawn
      if (nargs + 1 > STAPSH_MAX_ARGS)
        return reply ("ERROR: Too many arguments\n");
      if (qpdecode(arg) != 0)
        return reply ("ERROR: Invalid encoding in argument \"%s\"\n", arg);
      args[nargs++] = arg;
    }

  // Explicitly check execute permissions here, because posix_spawn will only
  // report that failure through a process exit code.
  if (access(staprun, X_OK) != 0)
    return reply ("ERROR: Can't execute %s (%s)\n", staprun, strerror(errno));

  // We pipe staprun under two conditions:
  // 1. The "data" option is on: we need to prefix all the output from staprun with data headers
  // 2. We're in listening mode: staprun needs to use the same port for
  //    writing, but would fail to open it since stapsh is already using it
  pid_t pid;
  if (prefix_data || listening_port != NULL)
    pid = spawn_staprun_piped(args);
  else
    pid = spawn_staprun(args);

  if (pid <= 0)
    {
      staprun_pid = fd_staprun_out = fd_staprun_err = -1;
      return reply ("ERROR: Failed to spawn staprun\n");
    }

  if (prefix_data || listening_port != NULL)
    {
      // make staprun fds non-blocking and prep polling array
      int flags;
      if ((flags = fcntl(fd_staprun_out, F_GETFL)) == -1
        || fcntl(fd_staprun_out, F_SETFL, flags | O_NONBLOCK) == -1
        ||(flags = fcntl(fd_staprun_err, F_GETFL)) == -1
        || fcntl(fd_staprun_err, F_SETFL, flags | O_NONBLOCK) == -1)
          return reply ("ERROR: Call to fcntl() failed");

      pfds[PFD_STAPRUN_OUT].fd = fd_staprun_out;
      pfds[PFD_STAPRUN_OUT].events = POLLIN;
      pfds[PFD_STAPRUN_ERR].fd = fd_staprun_err;
      pfds[PFD_STAPRUN_ERR].events = POLLIN;
    }

  staprun_pid = pid;
  reply ("OK\n");
  return 0;
}

static int
do_quit()
{
  cleanup(0);
}

static void
process_command(void)
{
  static int no_cmd_yet = 1;
  char command[4096];

  // This could technically block if only a partial command is received, but
  // stap commands are short and always end in \n. Even if we do block it's not
  // so bad a thing.
  if (fgets(command, sizeof(command), stapsh_in) == NULL)
    {
      if (feof(stapsh_in)) // no more stap commands coming
        pfds[PFD_STAP_OUT].events = 0;
      return;
    }

  dbug(1, "command: %s", command);
  const char* arg = strtok(command, STAPSH_TOK_DELIM) ?: "(null)";

  // If this is the first command and we're in listening mode, the only
  // acceptable command is "stap". This is necessary because there can be
  // data leftover in the buffer from a previous session
  if (no_cmd_yet && listening_port != NULL && strcmp(arg, "stap") != 0)
    return;
  no_cmd_yet = 0;

  unsigned i;
  for (i = 0; i < ncommands; ++i)
    if (strcmp(arg, commands[i].name) == 0)
      {
        int rc = commands[i].fn();
        if (rc)
          dbug(2, "failed command %s, rc=%d\n", arg, rc);
        break;
      }

  if (i >= ncommands)
    dbug(2, "invalid command %s\n", arg);
}

static void
prefix_staprun(int i, FILE *out, const char *stream)
{
  char buf[4096];
  ssize_t n = read(pfds[i].fd, buf, sizeof buf);
  if (n > 0)
    {
      // actually check if we need to prefix data (we could also be piping for
      // other reasons, e.g. listening_mode != NULL)
      if (prefix_data)
        fprintf(out, "data %s %zd\n", stream, n);
      if (fwrite(buf, n, 1, out) != 1)
        dbug(2, "failed fwrite\n"); // appease older gccs (don't ignore fwrite rc)
      fflush(out);
    }
  else if (n == 0) // eof
    pfds[i].events = 0;
}

int
main(int argc, char* const argv[])
{
  // set those right away so that *print* statements don't fail
  stapsh_in  = stdin;
  stapsh_out = stdout;
  stapsh_err = stderr;

  parse_args(argc, argv);

  // we're gonna need this for setup_signals so let's do it now
  if (uname(&uts))
    die("Error calling uname");

  setup_signals();

  if (listening_port != NULL)
    {
      listening_port_fd = open(listening_port, O_RDWR);
      if (listening_port_fd == -1)
        // no one might be watching but might as well print
        die("Error calling open()");

      if (can_sigio())
        {
          // make ourselves the recipient of SIGIO signals for this port
          if (fcntl(listening_port_fd, F_SETOWN, getpid()) < 0)
            die("Error calling fcntl F_SETOWN");

          // add O_ASYNC flag to enable SIGIO notifications on this port
          int flags = fcntl(listening_port_fd, F_GETFL);
          if (flags == -1)
            die("Error calling fcntl F_GETFL");
          if (fcntl(listening_port_fd, F_SETFL, flags | O_ASYNC) == -1)
            die("Error calling fcntl F_SETFL");
        }

      // We need to have different FILE* for each direction, otherwise
      // we'll have trouble down the line due to stdio's buffering
      // (doing "r+" on sockets is very wonky)
      stapsh_in  = fdopen(listening_port_fd, "r");
      stapsh_out = fdopen(listening_port_fd, "w");
      stapsh_err = stapsh_out;
      if (!stapsh_in || !stapsh_out)
        die("Could not open serial port");
    }

  umask(0077);
  snprintf(tmpdir, sizeof(tmpdir), "%s/stapsh.XXXXXX",
           getenv("TMPDIR") ?: "/tmp");
  if (!mkdtemp(tmpdir))
    die ("Can't make a temporary working directory");
  if (chdir(tmpdir))
    die ("Can't change to temporary working directory \"%s\"", tmpdir);

  // Prep pfds. For now we're only interested in commands from stap, and we
  // don't poll staprun until it is started.
  pfds[PFD_STAP_OUT].fd = fileno(stapsh_in);
  pfds[PFD_STAP_OUT].events = POLLIN;
  pfds[PFD_STAPRUN_OUT].events = 0;
  pfds[PFD_STAPRUN_ERR].events = 0;

  // Wait until a host connects before entering the command loop or poll() will
  // return with POLLHUP right away. Note that sleep() will be interrupted upon
  // SIGIO.
  if (listening_port != NULL)
    while (!host_connected())
      sleep(2); // Once we support only platforms with guaranteed SIGIO support,
                // we could replace this with a pause().

  // keep polling as long as we're listening for stap commands
  while (pfds[PFD_STAP_OUT].events)
    {
      if (poll(pfds, 3, -1) < 0)
        {
          if (errno == EINTR)
            continue; // go back to poll()
          else
            die ("poll() failed with critical error");
        }
      if (pfds[PFD_STAP_OUT].revents & POLLHUP)
        break;
      if (pfds[PFD_STAP_OUT].revents & POLLIN)
        process_command();
      if (pfds[PFD_STAPRUN_OUT].revents & POLLIN)
        prefix_staprun(PFD_STAPRUN_OUT, stapsh_out, "stdout");
      if (pfds[PFD_STAPRUN_ERR].revents & POLLIN)
        prefix_staprun(PFD_STAPRUN_ERR, stapsh_err, "stderr");
    }

  cleanup(0);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
