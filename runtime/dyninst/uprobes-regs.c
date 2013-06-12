/* -*- linux-c -*-
 * function for preparing registers for dyninst-based uprobes
 * Copyright (C) 2012-2013 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _UPROBES_REGS_DYNINST_C_
#define _UPROBES_REGS_DYNINST_C_

int enter_dyninst_uprobe_regs(uint64_t index, unsigned long nregs, ...)
{
	struct pt_regs regs = {};

	va_list varegs;
	va_start(varegs, nregs);

#ifdef __i386__
	// XXX Dyninst currently has a bug where it's only passing a 32-bit
	// index, which means nregs gets stuffed into the upper bits of index,
	// and the varegs are all off by one.  Hacking it into shape for now...
	if (index > UINT32_MAX) {
		SET_REG_IP((&regs), nregs);
                nregs = index >> 32;
                index &= UINT32_MAX;
        } else
#endif
	if (likely(nregs > 0))
		SET_REG_IP((&regs), va_arg(varegs, unsigned long));

#if defined(__i386__) || defined(__x86_64__)
	/* NB: x86 pt_regs_store_register() expects literal register numbers to
	 * paste as CPP tokens, so unfortunately this has to be unrolled.  */
#define SET_REG(n) if (likely(n < nregs - 1)) \
			pt_regs_store_register((&regs), n, \
					va_arg(varegs, unsigned long))
	SET_REG(0);
	SET_REG(1);
	SET_REG(2);
	SET_REG(3);
	SET_REG(4);
	SET_REG(5);
	SET_REG(6);
	SET_REG(7);
#if defined(__x86_64__)
	SET_REG(8);
	SET_REG(9);
	SET_REG(10);
	SET_REG(11);
	SET_REG(12);
	SET_REG(13);
	SET_REG(14);
	SET_REG(15);
#endif
#undef SET_REG

#else

#if defined(__powerpc__) || defined(__powerpc64__)
#define MAX_REG 32
#else
#error "Unknown architecture!"
#endif
	for (unsigned long r = 0; r < MAX_REG && r < nregs - 1; ++r)
		pt_regs_store_register((&regs), r,
				va_arg(varegs, unsigned long));
#undef MAX_REG

#endif

	va_end(varegs);

        return enter_dyninst_uprobe(index, &regs);
}

#endif /* _UPROBES_REGS_DYNINST_C_ */

