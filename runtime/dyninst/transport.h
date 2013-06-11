// stapdyn transport functions
// Copyright (C) 2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef TRANSPORT_H
#define TRANSPORT_H

#define STP_DYN_EXIT		0x0001
#define STP_DYN_NORMAL_DATA	0x0002
#define STP_DYN_OOB_DATA	0x0004
#define STP_DYN_SYSTEM		0x0008
#define STP_DYN_REQUEST_EXIT	0x0010

#define STP_DYN_OOB_DATA_MASK (STP_DYN_OOB_DATA | STP_DYN_SYSTEM)

// The size of print buffers. This limits the maximum amount of data a
// print can send. Note that it must be a power of 2 (which is why
// we're using a bit shift). For reference sake, here are the
// resulting sizes with various shifts: 13 (8kB), 14 (16kB), 15 (32kB),
// 16 (64kB), 17 (128kB), 18 (256kB), 19 (512kB), and 20 (1MB).
#ifndef STP_BUFFER_SIZE_SHIFT
#define STP_BUFFER_SIZE_SHIFT 20
#endif
#define _STP_DYNINST_BUFFER_SIZE (1 << STP_BUFFER_SIZE_SHIFT)

// The size of an individual log message.
#ifndef STP_LOG_BUF_LEN
#define STP_LOG_BUF_LEN 256
#endif
#if (STP_LOG_BUF_LEN < 10) /* sizeof(WARN_STRING) */
#error "STP_LOG_BUF_LEN is too short"
#endif

// The maximum number of log messages the buffer can hold. Note that
// it must be a power of 2 (which is why we're using a bit shift).
#ifndef STP_LOG_BUF_ENTRIES_SHIFT
#define STP_LOG_BUF_ENTRIES_SHIFT 4
#endif
#define _STP_LOG_BUF_ENTRIES (1 << STP_LOG_BUF_ENTRIES_SHIFT)

// The total size of the log buffer
#define _STP_DYNINST_LOG_BUF_LEN (STP_LOG_BUF_LEN * _STP_LOG_BUF_ENTRIES)

// The maximum number of queue items each transport queue can
// hold.
#ifndef STP_DYNINST_QUEUE_ITEMS
#define STP_DYNINST_QUEUE_ITEMS 64
#endif

struct _stp_transport_queue_item {
	// The type variable lets the thread know what it needs to do.
	unsigned type;

	// 'data_index' indicates which context structure we're
	// working on. */
	int data_index;

	// When 'type' indicates that normal or oob data needs to be
	// output, this is the data offset.
	size_t offset;

	// When 'type' indicates that normal or oob data needs to be
	// output, this is the number of bytes to output.
	size_t bytes;
};

struct _stp_transport_queue {
	size_t items;
	struct _stp_transport_queue_item queue[STP_DYNINST_QUEUE_ITEMS];
};

// This structure is stored in the session data.
struct _stp_transport_session_data {
	unsigned write_queue;
	struct _stp_transport_queue queues[2];
	pthread_mutex_t queue_mutex;
	pthread_cond_t queue_space_avail;
	pthread_cond_t queue_data_avail;
};

// This structure is stored in every context structure.
struct _stp_transport_context_data {
	/* The buffer and variables used for print messages */
	size_t read_offset;
	size_t write_offset;
	size_t write_bytes;
	char print_buf[_STP_DYNINST_BUFFER_SIZE];
	/* The condition variable is used to signal our thread. */
	pthread_cond_t print_space_avail;
	/* The lock for this print state. */
	pthread_mutex_t print_mutex;

	/*
	 * The buffer and variables used for log (warn/error)
	 * messages. Note that 'log_buf' is a true circular buffer
	 * (unlike print_buf). Also note that 'log_start' and
	 * 'log_end' are entry numbers (not offsets).
	 */
	size_t log_start;		/* index of oldest entry */
	size_t log_end;			/* where to write new entry */
	char log_buf[_STP_DYNINST_LOG_BUF_LEN];
	/* The condition variable is used to signal space available. */
	pthread_cond_t log_space_avail;
	/* The lock for 'log_space_avail'. */
	pthread_mutex_t log_mutex;
};

static int _stp_dyninst_transport_session_init(void);

static int _stp_dyninst_transport_session_start(void);

static int _stp_dyninst_transport_write_oob_data(char *buffer, size_t bytes);

static int _stp_dyninst_transport_write(void);

static char *_stp_dyninst_transport_log_buffer(void);

static void _stp_dyninst_transport_shutdown(void);

static void _stp_dyninst_transport_request_exit(void);

#endif // TRANSPORT_H
