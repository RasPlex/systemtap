/* -*- linux-c -*- 
 * Systemtap Memory1 Test Module
 * Copyright (C) 2013 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/compiler.h>
#include <asm/uaccess.h>

/* The purpose of this module is to provide a kernel string we can
 * safely read. We use a /proc file to trigger the function call from
 * user context.  Then systemtap scripts set probes on the functions
 * and run tests to see if the expected output is received. This is
 * better than using the kernel because kernel internals frequently
 * change. */

static struct proc_dir_entry *stm_ctl = NULL;
static char buffer[100] = "";

void noinline stp_memory1_set_str(const char *string, size_t bytes)
{
        asm ("");
	memcpy(buffer, string, bytes);
}

char * noinline stp_memory1_get_str(void)
{
        asm ("");
	return buffer;
}

EXPORT_SYMBOL(stp_memory1_set_str);
EXPORT_SYMBOL(stp_memory1_get_str);

static ssize_t stm_write (struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	char tmp[100];
	size_t bytes;

	bytes = (count >= sizeof(tmp) - 1) ? sizeof(tmp) - 1 : count;

	if (copy_from_user(tmp, buf, bytes))
		return -EFAULT;

	tmp[bytes] = '\0';
	stp_memory1_set_str(tmp, bytes);
	return bytes;
}

static ssize_t stm_read(struct file *file, char __user *buffer,
			size_t buflen, loff_t *fpos)
{
	char *data = stp_memory1_get_str();
	size_t bytes = strlen(data);

	if (buflen == 0 || *fpos >= bytes)
		return 0;

	bytes = min(bytes - (size_t)*fpos, buflen);
	if (copy_to_user(buffer, data + *fpos, bytes))
		return -EFAULT;
	*fpos += bytes;
	return bytes;
}

static struct file_operations stm_fops_cmd = {
	.owner = THIS_MODULE,
	.write = stm_write,
	.read = stm_read,
};

int init_module(void)
{
	stm_ctl = proc_create ("stap_memory1_test", 0666, NULL, &stm_fops_cmd);
	if (stm_ctl == NULL) 
		return -1;
	return 0;
}

void cleanup_module(void)
{
	if (stm_ctl)
		remove_proc_entry ("stap_memory1_test", NULL);
}

MODULE_DESCRIPTION("systemtap memory1 test module");
MODULE_LICENSE("GPL");
