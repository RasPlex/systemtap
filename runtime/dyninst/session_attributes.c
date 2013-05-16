// stapdyn session attribute code
// Copyright (C) 2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SESSION_ATTRIBUTES_C
#define SESSION_ATTRIBUTES_C

#include "session_attributes.h"

static struct _stp_session_attributes _stp_init_session_attributes = {
	.log_level = 0,
	.suppress_warnings = 0,
};

static void stp_session_attributes_init(void)
{
	stp_session_attributes()->log_level
	    = _stp_init_session_attributes.log_level;
	stp_session_attributes()->suppress_warnings
	    = _stp_init_session_attributes.suppress_warnings;
}

static int stp_session_attribute_setter(const char *name, const char *value)
{
	// Note that We start all internal variables with '@', since
	// that can't start a "real" variable.
	if (strcmp(name, "@log_level") == 0) {
		_stp_init_session_attributes.log_level = strtoul(value, NULL,
								 10);
		return 0;
	}
	else if (strcmp(name, "@suppress_warnings") == 0) {
		_stp_init_session_attributes.suppress_warnings
		    = strtoul(value, NULL, 10);
		return 0;
	}
	return -EINVAL;
}

#endif // SESSION_ATTRIBUTES_C
