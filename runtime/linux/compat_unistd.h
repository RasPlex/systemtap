/* -*- linux-c -*- 
 * Syscall compatibility defines.
 * Copyright (C) 2013 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _COMPAT_UNISTD_H_
#define _COMPAT_UNISTD_H_

#if defined(__x86_64__)

// On older kernels (like RHEL5), we have to define our own 32-bit
// syscall numbers.
#ifndef __NR_ia32_faccessat
#define __NR_ia32_faccessat 307
#endif
#ifndef __NR_ia32_fchmodat
#define __NR_ia32_fchmodat 306
#endif
#ifndef __NR_ia32_fchownat
#define __NR_ia32_fchownat 298
#endif

#define __NR_compat_faccessat		__NR_ia32_faccessat
#define __NR_compat_fchmodat		__NR_ia32_fchmodat
#define __NR_compat_fchownat		__NR_ia32_fchownat

#endif	/* __x86_64__ */

#if defined(__powerpc64__) || defined (__s390x__)

// On the ppc64 and s390x, the 32-bit syscalls use the same number
// as the 64-bit syscalls.

#define __NR_compat_faccessat		__NR_faccessat
#define __NR_compat_fchmodat		__NR_fchmodat
#define __NR_compat_fchownat		__NR_fchownat

#endif	/* __powerpc64__ || __s390x__ */

#endif /* _COMPAT_UNISTD_H_ */
