/* -*- linux-c -*- 
 * Print Functions
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPDYN_PRINT_C_
#define _STAPDYN_PRINT_C_

#ifdef STP_BULKMODE
#error "Bulk mode output (percpu files) not supported for --runtime=dyninst"
#endif
#ifdef STP_USE_RING_BUFFER
#error "Ring buffer output not supported for --runtime=dyninst"
#endif
#if defined(RELAY_GUEST) || defined(RELAY_HOST)
#error "Relay host/guest output not supported for --runtime=dyninst"
#endif

#include "transport.c"
#include "vsprintf.c"

static void _stp_print_kernel_info(char *vstr, int ctx, int num_probes)
{
	// nah...
}

static int _stp_print_init(void)
{
	/* Since 'print_buf' is now in the context structure (see
	 * common_probe_context.h), there isn't anything to do for
	 * regular print buffers. */
	return 0;
}

static void _stp_print_cleanup(void)
{
	/* Since 'print_buf' is now in the context structure (see
	 * common_probe_context.h), there isn't anything to free for
	 * it. */
}

static inline void _stp_print_flush(void)
{
	_stp_dyninst_transport_write();
	return;
}

static void * _stp_reserve_bytes (int numbytes)
{
	return _stp_dyninst_transport_reserve_bytes(numbytes);
}

#ifndef STP_MAXBINARYARGS
#define STP_MAXBINARYARGS 127
#endif

static void _stp_unreserve_bytes (int numbytes)
{
	_stp_dyninst_transport_unreserve_bytes(numbytes);
	return;
}

/** Write 64-bit args directly into the output stream.
 * This function takes a variable number of 64-bit arguments
 * and writes them directly into the output stream.  Marginally faster
 * than doing the same in _stp_vsnprintf().
 * @sa _stp_vsnprintf()
 */
static void _stp_print_binary (int num, ...)
{
	va_list vargs;
	int i;
	int64_t *args;
	
	if (unlikely(num > STP_MAXBINARYARGS))
		num = STP_MAXBINARYARGS;

	args = _stp_reserve_bytes(num * sizeof(int64_t));

	if (likely(args != NULL)) {
		va_start(vargs, num);
		for (i = 0; i < num; i++) {
			args[i] = va_arg(vargs, int64_t);
		}
		va_end(vargs);
	}
}

static void _stp_printf (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vsnprintf(NULL, 0, fmt, args);
	va_end(args);
}

static void _stp_print (const char *str)
{
	_stp_printf("%s", str);
}

static void _stp_print_char (const char c)
{
	char *p = _stp_reserve_bytes(1);;

	if (p) {
		*p = c;
	}
}

#endif /* _STAPDYN_PRINT_C_ */
