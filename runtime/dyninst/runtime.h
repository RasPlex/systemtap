/* main dyninst header file
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPDYN_RUNTIME_H_
#define _STAPDYN_RUNTIME_H_

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/ptrace.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "loc2c-runtime.h"
#include "stapdyn.h"

#if __WORDSIZE == 64
#define CONFIG_64BIT 1
#endif

#define BITS_PER_LONG __WORDSIZE

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#include "linux_types.h"
#include "offset_list.h"


#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif

#define _stp_timespec_sub(a, b, result)					      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;			      \
    if ((result)->tv_nsec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_nsec += NSEC_PER_SEC;				      \
    }									      \
  } while (0)

#define simple_strtol strtol


// segments don't matter in dyninst...
#define USER_DS (1)
#define KERNEL_DS (-1)
typedef int mm_segment_t;
static inline mm_segment_t get_fs(void) { return 0; }
static inline void set_fs(mm_segment_t seg) { (void)seg; }


static inline int atomic_add_return(int i, atomic_t *v)
{
	return __sync_add_and_fetch(&(v->counter), i);
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	return __sync_sub_and_fetch(&(v->counter), i);
}

static inline int pseudo_atomic_cmpxchg(atomic_t *v, int oldval, int newval)
{
	return __sync_val_compare_and_swap(&(v->counter), oldval, newval);
}

#include "linux_defs.h"

#define MODULE_DESCRIPTION(str)
#define MODULE_LICENSE(str)
#define MODULE_INFO(tag,info)

/* Semi-forward declarations from runtime_context.h, needed by stat.c/shm.c. */
static int _stp_runtime_num_contexts;

/* Semi-forward declarations from this file, needed by stat.c/transport.c. */
static int stp_pthread_mutex_init_shared(pthread_mutex_t *mutex);
static int stp_pthread_cond_init_shared(pthread_cond_t *cond);

#define for_each_possible_cpu(cpu) for ((cpu) = 0; (cpu) < _stp_runtime_num_contexts; (cpu)++)

#define yield() sched_yield()

#define access_ok(type, addr, size) 1

#define preempt_disable() 0
#define preempt_enable_no_resched() 0

static int _stp_sched_getcpu(void)
{
    /* We prefer sched_getcpu directly, of course.  It wasn't added until glibc
     * 2.6 though, and has no direct feature indication, but CPU_ZERO_S was
     * added shortly after too.  */
#ifdef CPU_ZERO_S
    return sched_getcpu();

#elif defined(SYS_getcpu)
    /* A manual getcpu is fine too, though not necessarily as fast since it
     * can't be optimized as a vdso/vsyscall.  */
    unsigned cpu;
    int ret = syscall(SYS_getcpu, &cpu, NULL, NULL);
    return (ret < 0) ? ret : (int)cpu;

#else
    /* XXX Any other way to find our cpu?  Manual vgetcpu? */
    return -2;
#endif
}

/* see common_session_state.h */
static inline struct _stp_transport_session_data *stp_transport_data(void);
static inline struct _stp_session_attributes *stp_session_attributes(void);

/*
 * By definition, we can only debug our own processes with dyninst, so
 * assert_is_myproc() will never assert.
 *
 * FIXME: We might want to add the check back, to get a better error
 * message.
 */
#define assert_is_myproc() do {} while (0)

#include "debug.h"

#include "io.c"
#include "alloc.c"
#include "shm.c"
#include "print.h"
#include "stp_string.c"
#include "arith.c"
#include "copy.c"
#include "../regs.c"
#include "task_finder.c"
#include "sym.c"
#include "perf.c"
#include "addr-map.c"
#include "stat.c"
#include "unwind.c"
#include "session_attributes.c"

/* Support function for int64_t module parameters. */
static int set_int64_t(const char *val, int64_t *mp)
{
  char *endp;
  long long ll;

  if (!val)
    return -EINVAL;

  ll = strtoull(val, &endp, 0);

  if ((endp == val) || ((int64_t)ll != ll) || (*endp != '\0'))
    return -EINVAL;

  *mp = (int64_t)ll;
  return 0;
}

static int systemtap_module_init(void);
static void systemtap_module_exit(void);

static inline unsigned long _stap_hash_seed(); /* see common_session_state.h */
#define stap_hash_seed _stap_hash_seed()

typedef pthread_rwlock_t rwlock_t; /* for globals */

static int stp_pthread_mutex_init_shared(pthread_mutex_t *mutex)
{
	int rc;
	pthread_mutexattr_t attr;

	rc = pthread_mutexattr_init(&attr);
	if (rc != 0) {
	    _stp_error("pthread_mutexattr_init failed");
	    return rc;
	}

	rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	if (rc != 0) {
	    _stp_error("pthread_mutexattr_setpshared failed");
	    goto err_attr;
	}

	rc = pthread_mutex_init(mutex, &attr);
	if (rc != 0) {
	    _stp_error("pthread_mutex_init failed");
	    goto err_attr;
	}

err_attr:
	(void)pthread_mutexattr_destroy(&attr);
	return rc;
}

static int stp_pthread_cond_init_shared(pthread_cond_t *cond)
{
	int rc;
	pthread_condattr_t attr;

	rc = pthread_condattr_init(&attr);
	if (rc != 0) {
	    _stp_error("pthread_condattr_init failed");
	    return rc;
	}

	rc = pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	if (rc != 0) {
	    _stp_error("pthread_condattr_setpshared failed");
	    goto err_attr;
	}

	rc = pthread_cond_init(cond, &attr);
	if (rc != 0) {
	    _stp_error("pthread_cond_init failed");
	    goto err_attr;
	}

err_attr:
	(void)pthread_condattr_destroy(&attr);
	return rc;
}

static int stp_pthread_rwlock_init_shared(pthread_rwlock_t *rwlock)
{
	int rc;
	pthread_rwlockattr_t attr;

	rc = pthread_rwlockattr_init(&attr);
	if (rc != 0) {
	    _stp_error("pthread_rwlockattr_init failed");
	    return rc;
	}

	rc = pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	if (rc != 0) {
	    _stp_error("pthread_rwlockattr_setpshared failed");
	    goto err_attr;
	}

	rc = pthread_rwlock_init(rwlock, &attr);
	if (rc != 0) {
	    _stp_error("pthread_rwlock_init failed");
	    goto err_attr;
	}

err_attr:
	(void)pthread_rwlockattr_destroy(&attr);
	return rc;
}

/*
 * For stapdyn to work in a multiprocess environment, the module must be
 * prepared to be loaded multiple times in different processes.  Thus, we have
 * to distinguish between process-level resource initialization and
 * "session"-level (like probe begin/end/error).
 *
 * So stp_dyninst_ctor/dtor are the process-level functions, using gcc
 * attributes to get called at the right time.  One startup exception is
 * stp_dyninst_shm_connect, which has to later be called manually with the shm
 * path being used in this session.
 *
 * The session-level resources have to be started by the stapdyn mutator, and
 * are called within stapdyn itself.  This primarily involves allocating the
 * shared memory and initializing its contents, but also running those global
 * begin/end/error probes.
 *
 * NB: We used to keep code that tried to deal with stapdyn 2.0, which didn't
 * know about shm initialization, and ran everything in the mutatees only.
 * We've now broken that tie, and stp_dyninst_session_init will detect that
 * shm wasn't initialized and bow out.
 */

static int _stp_runtime_contexts_init(void);

static int stp_dyninst_ctor_rc = 0;

__attribute__((constructor))
static void stp_dyninst_ctor(void)
{
    int rc = 0;

    _stp_mem_fd = open("/proc/self/mem", O_RDWR /*| O_LARGEFILE*/);
    if (_stp_mem_fd != -1) {
        fcntl(_stp_mem_fd, F_SETFD, FD_CLOEXEC);
    }
    else {
        rc = -errno;
    }

    if (rc == 0)
        rc = _stp_runtime_contexts_init();

    if (rc == 0)
        rc = _stp_print_init();

    stp_dyninst_ctor_rc = rc;
}

const char* stp_dyninst_shm_init(void)
{
    return _stp_shm_init();
}

int stp_dyninst_shm_connect(const char* name)
{
    int rc;

    /* We don't have a chance to indicate errors in the ctor, so do it here. */
    if (stp_dyninst_ctor_rc != 0) {
	return stp_dyninst_ctor_rc;
    }

    rc = _stp_shm_connect(name);
    return rc;
}

int stp_dyninst_session_init(void)
{
    /* We don't have a chance to indicate errors in the ctor, so do it here. */
    if (stp_dyninst_ctor_rc != 0) {
	return stp_dyninst_ctor_rc;
    }

    /* If shared memory has not been initialized yet, we're probably dealing
     * with stapdyn 2.0 -- we no longer support this case.  */
    if (_stp_shm_base == NULL)
	return -ENOMEM;

    return systemtap_module_init();
}

/* This is called during systemtap_module_init, after globals/etc are set up,
 * but before any probes are actually executed.
 * (Perhaps it would be cleaner if the translator split those stages?)
 */
static int stp_dyninst_session_init_finished(void)
{
    _stp_shm_finalize();

    /* Now that the shared memory is finalized, start the
     * transport. If we started it before now, allocations could have
     * caused the base address of the shared memory to move around,
     * which would cause the addresses of the mutexes to move
     * around. */
    return _stp_dyninst_transport_session_start();
}

void stp_dyninst_session_exit(void)
{
    systemtap_module_exit();
}

static int _stp_exit_status = 0;
int stp_dyninst_exit_status(void)
{
    return _stp_exit_status;
}

__attribute__((destructor))
static void stp_dyninst_dtor(void)
{
    _stp_print_cleanup();
    _stp_shm_destroy();

    if (_stp_mem_fd != -1) {
	close (_stp_mem_fd);
    }
}

#endif /* _STAPDYN_RUNTIME_H_ */
