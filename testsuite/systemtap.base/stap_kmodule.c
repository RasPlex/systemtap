/* -*- linux-c -*- 
 * Systemtap Test Module 1
 * Copyright (C) 2007 Red Hat Inc.
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

/*
 * The purpose of this module is to provide a function that can be
 * triggered from user context via a /proc file.  Systemtap scripts
 * set probes on the function and run tests to see if the expected
 * output is received. This is better than using the kernel's modules
 * because kernel internals frequently change.
 */

/************ Below are the functions to create this module ************/

static struct proc_dir_entry *stm_ctl = NULL;

static int last_cmd = -1;

static ssize_t stm_write_cmd (struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	char type;

	if (get_user(type, (char __user *)buf))
		return -EFAULT;

	switch (type) {
	case '0':
		last_cmd = 0;
		break;
	case '1':
		last_cmd = 1;
		break;
	case '2':
		last_cmd = 2;
		break;
	default:
		printk ("stap_kmodule: invalid command type %d\n", (int)type);
		return -EINVAL;
	}
  
	return count;
}

static struct file_operations stm_fops_cmd = {
	.owner = THIS_MODULE,
	.write = stm_write_cmd,
};

int init_module(void)
{
	stm_ctl = proc_create ("stap_kmodule_cmd", 0666, NULL, &stm_fops_cmd);
	if (stm_ctl == NULL) 
		return -1;
	return 0;
}

void cleanup_module(void)
{
	if (stm_ctl)
		remove_proc_entry ("stap_kmodule_cmd", NULL);
}

MODULE_DESCRIPTION("systemtap test module");
MODULE_LICENSE("GPL");
