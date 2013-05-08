/* -*- linux-c -*- 
 * I/O for printing warnings, errors and debug messages
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPDYN_IO_C_
#define _STAPDYN_IO_C_

#include <sys/param.h>
#include "transport.h"
#include "vsprintf.h"
#include "print.h"

#define WARN_STRING "WARNING: "
#define ERR_STRING "ERROR: "

// XXX for now, all IO is going in-process to stdout/stderr via
// _stp_out/_stp_err; see runtime/dyninst/runtime.h for initialization.

static FILE* _stp_out = NULL;
static FILE* _stp_err = NULL;


/* Clone a FILE* for private use.  On error, fallback to the original. */
static FILE* _stp_clone_file(FILE* file)
{
    int fd = dup(fileno(file));
    if (fd != -1) {
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	FILE* newfile = fdopen(fd, "wb");
	if (newfile)
	    return newfile;

	close(fd);
    }
    return file;
}

enum code { INFO=0, WARN, ERROR, DBUG };

static void _stp_vlog (enum code type, const char *func, int line,
		       const char *fmt, va_list args)
	__attribute ((format (printf, 4, 0)));

static void _stp_vlog (enum code type, const char *func, int line,
		       const char *fmt, va_list args)
{
	size_t start = 0;
	size_t num;
	char tmp[STP_LOG_BUF_LEN];
	char *buf = _stp_dyninst_transport_log_buffer();

	/* If we can't get a buffer, the transport must be not up yet.
	 * Instead use our temporary buffer. */
	if (buf == NULL)
		buf = tmp;

	if (type == DBUG) {
		start = snprintf(buf, STP_LOG_BUF_LEN, "%s:%d: ", func, line);
	}
	else if (type == WARN) {
		/* This strcpy() is OK, since we know STP_LOG_BUF_LEN
		 * is > sizeof(WARN_STRING). */
		strcpy(buf, WARN_STRING);
		start = sizeof(WARN_STRING)- 1;
	}
	else if (type == ERROR) {
		/* This strcpy() is OK, since we know STP_LOG_BUF_LEN
		 * is > sizeof(ERR_STRING) (which is <
		 * sizeof(WARN_STRING). */
		strcpy(buf, ERR_STRING);
		start = sizeof(ERR_STRING) - 1;
	}

	/* Note that if the message is too long it will just get truncated. */
	num = _stp_vsnprintf(buf + start, STP_LOG_BUF_LEN - start - 1,
			     fmt, args);
	if ((num + start) == 0)
		return;

	/* If the last character is not a newline, then add one. */
	if (buf[num + start - 1] != '\n') {
		/* Make sure we don't overflow the buffer. */
		if ((num + start + 1) >= STP_LOG_BUF_LEN)
			num--;
		buf[num + start] = '\n';
		num++;
		buf[num + start] = '\0';
	}

	/* If we're using the temporary buffer, just output it to
	 * _stp_err. */
	if (buf == tmp) {
		fprintf(_stp_err, "%s", buf);
	}
	else if (type != DBUG) {
                /* NB: don't explicitly send the \0 terminator. */
		_stp_dyninst_transport_write_oob_data(buf, num + start);
	}
	else {
		_stp_print(buf);
		_stp_print_flush();
	}
}

/** Prints warning.
 * This function sends a warning message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 * @param fmt A variable number of args.
 */
static void _stp_warn (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (WARN, NULL, 0, fmt, args);
	va_end(args);
}


/** Prints error message and exits.
 * This function sends an error message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 *
 * After the error message is displayed, the module will be unloaded.
 * @param fmt A variable number of args.
 * @sa _stp_exit().
 */
static void _stp_error (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (ERROR, NULL, 0, fmt, args);
	va_end(args);
// FIXME: need to exit here...
//	_stp_exit();
}


/** Prints error message.
 * This function sends an error message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 *
 * @param fmt A variable number of args.
 * @sa _stp_error
 */
static void _stp_softerror (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (ERROR, NULL, 0, fmt, args);
	va_end(args);
}


static void _stp_dbug (const char *func, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (DBUG, func, line, fmt, args);
	va_end(args);
}

#define _stp_exit() do { } while (0) // no transport, no action yet

#endif /* _STAPDYN_IO_C_ */
