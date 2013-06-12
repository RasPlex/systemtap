// stapdyn session attribute declarations
// Copyright (C) 2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SESSION_ATTRIBUTES_H
#define SESSION_ATTRIBUTES_H

struct _stp_session_attributes {
	unsigned long log_level;
	unsigned long suppress_warnings;
	unsigned long stp_pid;
	unsigned long target;
	long tz_gmtoff;
	char tz_name[MAXSTRINGLEN];
	char module_name[MAXSTRINGLEN];
	char outfile_name[PATH_MAX];
};

static void stp_session_attributes_init(void);
static int stp_session_attribute_setter(const char *name, const char *value);

#endif // SESSION_ATTRIBUTES_H
