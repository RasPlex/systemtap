/* -*- linux-c -*-
 *
 * strfloctime.c - staprun/io time-based formatting
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007-2013 Red Hat Inc.
 */

#include "staprun.h"
#include <time.h>

int stap_strfloctime(char *buf, size_t max, const char *fmt, time_t t)
{
	struct tm tm;
	size_t ret;
	if (buf == NULL || fmt == NULL || max <= 1)
		return -EINVAL;
	localtime_r(&t, &tm);
	/* NB: this following invocation is the reason for this CU being built
	   with -Wno-format-nonliteral.  strftime parsing does not have security
	   implications AFAIK, but gcc still wants to check them.  */
	ret = strftime(buf, max, fmt, &tm);
	if (ret == 0)
		return -EINVAL;
	return (int)ret;
}

