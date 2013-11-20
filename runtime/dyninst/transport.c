/* -*- linux-c -*- 
 * Transport Functions
 * Copyright (C) 2013 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPDYN_TRANSPORT_C_
#define _STAPDYN_TRANSPORT_C_

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <spawn.h>

#include <sys/syscall.h>

#include <errno.h>
#include <string.h>
#include <search.h>
#include <signal.h>

#include "transport.h"

////////////////////////////////////////
//
// GENERAL TRANSPORT OVERVIEW
//
// Each context structure has a '_stp_transport_context_data'
// structure (described in more detail later) in it, which contains
// that context's print and log (warning/error) buffers. There is a
// session-wide double-buffered queue (stored in the
// '_stp_transport_session_data' structure) where each probe can send
// print/control messages to a fairly simple consumer thread (see
// _stp_dyninst_transport_thread_func() for details). The consumer
// thread swaps the read/write queues, then handles each request.
//
// Note that there is as little as possible data copying going on. A
// probe adds data to a print/log buffer stored in shared memory, then
// the consumer queue outputs the data from that same buffer.
//
//
// QUEUE OVERVIEW
//
// See the session-wide queue's definition in transport.h. It is
// composed of the '_stp_transport_queue_item', '_stp_transport_queue'
// and '_stp_transport_session_data' structures.
//
// The queue is double-buffered and stored in shared memory. Because
// it is session-wide, and multiple threads can be trying to add data
// to it simultaneously, the 'queue_mutex' is used to serialize
// access.  Probes write to the write queue. When the consumer thread
// realizes data is available, it swaps the read/write queues (by
// changing the 'write_queue' value) and then processes each
// '_stp_transport_queue_item' on the read queue.
//
// If the queue is full, probes will wait on the 'queue_space_avail'
// condition variable for more space. The consumer thread sets
// 'queue_space_avail' when it swaps the read/write queues.
//
// The consumer thread waits on the 'queue_data_avail' condition
// variable to know when more items are available. When probes add
// items to the queue (using __stp_dyninst_transport_queue_add()),
// 'queue_data_avail' gets set.
//
// 
// LOG BUFFER OVERVIEW
//
// See the context-specific log buffer's (struct
// _stp_transport_context_data) definition in transport.h.
//
// The log buffer, used for warning/error messages, is stored in
// shared memory. Each context structure has its own log buffer. Each
// log buffer logically contains '_STP_LOG_BUF_ENTRIES' buffers of
// length 'STP_LOG_BUF_LEN'. In other words, the log buffer allocation
// is done in chunks of size 'STP_LOG_BUF_LEN'.  The log buffer is
// circular, and the indices use an extra most significant bit to
// indicate wrapping.
//
// Only the consumer thread removes items from the log buffer.  The
// log buffer is circular, and the indices use an extra most
// significant bit to indicate wrapping.
//
// If the log buffer is full, probes will wait on the
// 'log_space_avail' condition variable for more space. The consumer
// thread sets 'log_space_avail' after finishing with a particular log
// buffer chunk.
//
// Note that the read index 'log_start' is only written to by the
// consumer thread and that the write index 'log_end' is only written
// to by the probes (with a locked context).
//
//
// PRINT BUFFER OVERVIEW
//
// See the context-specific print buffer definition (struct
// _stp_transport_context_data) in transport.h.
//
// The print buffer is stored in shared memory. Each context structure
// has its own print buffer.  The print buffer really isn't a true
// circular buffer, it is more like a "semi-cicular" buffer. If a
// reservation request won't fit after the write offset, we go ahead
// and wrap around to the beginning (if available), leaving an unused
// gap at the end of the buffer. This is done to not break up
// reservation requests.  Like a circular buffer, the offsets use an
// extra most significant bit to indicate wrapping.
//
// Only the consumer thread (normally) removes items from the print
// buffer. It is possible to 'unreserve' bytes using
// _stp_dyninst_transport_unreserve_bytes() if the bytes haven't been
// flushed.
//
// If the print buffer doesn't have enough bytes available, probes
// will flush any reserved bytes earlier than normal, then wait on the
// 'print_space_avail' condition variable for more space to become
// available. The consumer thread sets 'print_space_avail' after
// finishing with a particular print buffer segment.
//
// Note that the read index 'read_offset' is only written to by the
// consumer thread and that the write index 'write_offset' (and number
// of bytes to write 'write_bytes) is only written to by the probes
// (with a locked context).
//
////////////////////////////////////////

static pthread_t _stp_transport_thread;
static int _stp_transport_thread_started = 0;

#ifndef STP_DYNINST_TIMEOUT_SECS
#define STP_DYNINST_TIMEOUT_SECS 5
#endif

// When we're converting an circular buffer/index into a pointer
// value, we need the "normalized" value (i.e. one without the extra
// msb possibly set).
#define _STP_D_T_LOG_NORM(x)	((x) & (_STP_LOG_BUF_ENTRIES - 1))
#define _STP_D_T_PRINT_NORM(x)	((x) & (_STP_DYNINST_BUFFER_SIZE - 1))

// Define a macro to generically add circular buffer
// offsets/indicies.
#define __STP_D_T_ADD(offset, increment, buffer_size) \
	(((offset) + (increment)) & (2 * (buffer_size) - 1))

// Using __STP_D_T_ADD(), define a specific macro for each circular
// buffer.
#define _STP_D_T_LOG_INC(offset) \
	__STP_D_T_ADD((offset), 1, _STP_LOG_BUF_ENTRIES)
#define _STP_D_T_PRINT_ADD(offset, increment) \
	__STP_D_T_ADD((offset), (increment), _STP_DYNINST_BUFFER_SIZE)

// Return a pointer to the session's current write queue.
#define _STP_D_T_WRITE_QUEUE(sess_data) \
	(&((sess_data)->queues[(sess_data)->write_queue]))

// Limit remembered strings in __stp_d_t_eliminate_duplicate_warnings
#define MAX_STORED_WARNINGS 1024


// If the transport has an error or debug message to print, it can't very well
// recurse on itself, so we just print to the local stderr and hope...
static void _stp_transport_err (const char *fmt, ...)
	__attribute ((format (printf, 1, 2)));
static void _stp_transport_err (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf (stderr, fmt, args);
	va_end(args);
}

#ifdef DEBUG_TRANS
#define _stp_transport_debug(fmt, ...) \
    _stp_transport_err("%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define _stp_transport_debug(fmt, ...) do { } while(0)
#endif


static void
__stp_dyninst_transport_queue_add(unsigned type, int data_index,
				  size_t offset, size_t bytes)
{
	struct _stp_transport_session_data *sess_data = stp_transport_data();

	if (sess_data == NULL)
		return;

	pthread_mutex_lock(&(sess_data->queue_mutex));
	// While the write queue is full, wait.
	while (_STP_D_T_WRITE_QUEUE(sess_data)->items
	       == (STP_DYNINST_QUEUE_ITEMS - 1)) {
		pthread_cond_wait(&(sess_data->queue_space_avail),
				  &(sess_data->queue_mutex));
	}
	struct _stp_transport_queue *q = _STP_D_T_WRITE_QUEUE(sess_data);
	struct _stp_transport_queue_item *item = &(q->queue[q->items]);
	q->items++;
	item->type = type;
	item->data_index = data_index;
	item->offset = offset;
	item->bytes = bytes;
        pthread_cond_signal(&(sess_data->queue_data_avail));
	pthread_mutex_unlock(&(sess_data->queue_mutex));
}

/* Handle duplicate warning elimination. Returns 0 if we've seen this
 * warning (and should be eliminated), 1 otherwise. */
static int
__stp_d_t_eliminate_duplicate_warnings(char *data, size_t bytes)
{
	static void *seen = 0;
	static unsigned seen_count = 0;
	char *dupstr = strndup (data, bytes);
	char *retval;
	int rc = 1;

	if (! dupstr) {
		/* OOM, should not happen. */
		return 1;
	}

	retval = tfind (dupstr, &seen,
			(int (*)(const void*, const void*))strcmp);
	if (! retval) {			/* new message */
		/* We set a maximum for stored warning messages, to
		 * prevent a misbehaving script/environment from
		 * emitting countless _stp_warn()s, and overflow
		 * staprun's memory. */
		if (seen_count++ == MAX_STORED_WARNINGS) {
			_stp_transport_err("WARNING deduplication table full\n");
			free (dupstr);
		}
		else if (seen_count > MAX_STORED_WARNINGS) {
			/* Be quiet in the future, but stop counting
			 * to preclude overflow. */
			free (dupstr);
			seen_count = MAX_STORED_WARNINGS + 1;
		}
		else if (seen_count < MAX_STORED_WARNINGS) {
			/* NB: don't free dupstr; it's going into the tree. */
			retval = tsearch (dupstr, & seen,
					  (int (*)(const void*, const void*))strcmp);
			if (retval == 0) {
				/* OOM, should not happen.  Next time
				 * we should get the 'full'
				 * message. */
				free (dupstr);
				seen_count = MAX_STORED_WARNINGS;
			}
		}
	}
	else {				/* old message */
		free (dupstr);
		rc = 0;
	}
	return rc;
}

static void
__stp_d_t_run_command(char *command)
{
	/*
	 * FIXME: We'll need to make sure the output from system goes
	 * to the correct file descriptor. We may need some posix file
	 * actions to pass to posix_spawnp().
	 */
	char *spawn_argv[4] = { "sh", "-c", command, NULL };
	int rc = posix_spawnp(NULL, "sh", NULL, NULL, spawn_argv, NULL);
	if (rc != 0) {
		_stp_transport_err("ERROR: %s : %s\n", command, strerror(rc));
	}
	/* Notice we're not waiting on the resulting process to finish. */
}

static void
__stp_d_t_request_exit(void)
{
	/*
	 * We want stapdyn to trigger this module's exit code from outside.  It
	 * knows to do this on receipt of signals, so we must kill ourselves.
	 * The signal handler will forward that to the main thread.
	 *
	 * NB: If the target process was created rather than attached, SIGTERM
	 * waits for it to exit.  SIGQUIT always exits immediately.  It's
	 * somewhat debateable which is most appropriate here...
	 */
	pthread_kill(pthread_self(), SIGTERM);
}

static ssize_t
_stp_write_retry(int fd, const void *buf, size_t count)
{
	size_t remaining = count;
	while (remaining > 0) {
		ssize_t ret = write(fd, buf, remaining);
		if (ret >= 0) {
			buf += ret;
			remaining -= ret;
		}
		else if (errno != EINTR) {
			return ret;
		}
	}
	return count;
}

static int
stap_strfloctime(char *buf, size_t max, const char *fmt, time_t t)
{
	struct tm tm;
	size_t ret;
	if (buf == NULL || fmt == NULL || max <= 1)
		return -EINVAL;
	localtime_r(&t, &tm);
	/* NB: this following invocation means that stapdyn modules can't be
	   checked with -Wformat-nonliteral.  See compile_dyninst()
	   buildrun.cxx for the flags chosen.  strftime parsing does not have
	   security implications AFAIK, but gcc still wants to check them.  */
	ret = strftime(buf, max, fmt, &tm);
	if (ret == 0)
		return -EINVAL;
	return (int)ret;
}

static void *
_stp_dyninst_transport_thread_func(void *arg __attribute((unused)))
{
	int stopping = 0;
	int out_fd, err_fd;
	struct _stp_transport_session_data *sess_data = stp_transport_data();

	if (sess_data == NULL)
		return NULL;

	if (strlen(stp_session_attributes()->outfile_name)) {
		char buf[PATH_MAX];
		int rc;

		rc = stap_strfloctime(buf, PATH_MAX,
				      stp_session_attributes()->outfile_name,
				      time(NULL));
		if (rc < 0) {
			_stp_transport_err("Invalid FILE name format\n");
			return NULL;
		}
		out_fd = open (buf, O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC, 0666);
		if (out_fd < 0) {
			_stp_transport_err("ERROR: Couldn't open output file %s: %s\n",
					   buf, strerror(rc));
			return NULL;
		}
	}
	else
		out_fd = STDOUT_FILENO;
	err_fd = STDERR_FILENO;
	if (out_fd < 0 || err_fd < 0)
		return NULL;

	while (! stopping) {
		struct _stp_transport_queue *q;
		struct _stp_transport_queue_item *item;
		struct context *c;
		struct _stp_transport_context_data *data;
		void *read_ptr;

		pthread_mutex_lock(&(sess_data->queue_mutex));
		// While there are no queue entries, wait.
		q = _STP_D_T_WRITE_QUEUE(sess_data);
		while (q->items == 0) {
			// Mutex is locked. It is automatically
			// unlocked while we are waiting.
			pthread_cond_wait(&(sess_data->queue_data_avail),
					  &(sess_data->queue_mutex));
			// Mutex is locked again.
		}

		// We've got data. Swap the queues and let any waiters
		// know there is more space available.
		sess_data->write_queue ^= 1;
		pthread_cond_broadcast(&(sess_data->queue_space_avail));
		pthread_mutex_unlock(&(sess_data->queue_mutex));

		// Note that we're processing the read queue with no
		// locking. This is possible since no other thread
		// will be accessing it until we're finished with it
		// (and we make it the write queue).

		// Process the queue twice. First handle the OOB data types.
		for (size_t i = 0; i < q->items; i++) {
			int write_data = 1;
			item = &(q->queue[i]);
			if (! (item->type & STP_DYN_OOB_DATA_MASK))
				continue;

			c = stp_session_context(item->data_index);
			data = &c->transport_data;
			read_ptr = data->log_buf + item->offset;

			switch (item->type) {
			case STP_DYN_OOB_DATA:
				_stp_transport_debug(
                                        "STP_DYN_OOB_DATA (%ld bytes at offset %ld)\n",
					item->bytes, item->offset);

				/* Note that "WARNING:" should not be
				 * translated, since it is part of the
				 * module cmd protocol. */
				if (strncmp(read_ptr, "WARNING:", 7) == 0) {
					if (stp_session_attributes()->suppress_warnings) {
						write_data = 0;
					}
					/* If we're not verbose, eliminate
					 * duplicate warning messages. */
					else if (stp_session_attributes()->log_level
						 == 0) {
						write_data = __stp_d_t_eliminate_duplicate_warnings(read_ptr, item->bytes);
					}
				}
				/* "ERROR:" also should not be translated.  */
				else if (strncmp(read_ptr, "ERROR:", 5) == 0) {
					if (_stp_exit_status == 0)
						_stp_exit_status = 1;
				}

				if (! write_data) {
					break;
				}

				if (_stp_write_retry(err_fd, read_ptr, item->bytes) < 0)
					_stp_transport_err(
						"couldn't write %ld bytes OOB data: %s\n",
						(long)item->bytes, strerror(errno));
				break;

			case STP_DYN_SYSTEM:
				_stp_transport_debug("STP_DYN_SYSTEM (%.*s) %d bytes\n",
					(int)item->bytes, (char *)read_ptr,
					(int)item->bytes);
				/*
				 * Note that the null character is
				 * already included in the system
				 * string.
				 */
				__stp_d_t_run_command(read_ptr);
				break;
			default:
				_stp_transport_err(
					"Error - unknown OOB item type %d\n",
					item->type);
				break;
			}

			// Signal there is a log buffer available to
			// any waiters.
			data->log_start = _STP_D_T_LOG_INC(data->log_start);
			pthread_mutex_lock(&(data->log_mutex));
			pthread_cond_signal(&(data->log_space_avail));
			pthread_mutex_unlock(&(data->log_mutex));
		}

		// Handle the non-OOB data.
		for (size_t i = 0; i < q->items; i++) {
			item = &(q->queue[i]);

			switch (item->type) {
			case STP_DYN_NORMAL_DATA:
				_stp_transport_debug("STP_DYN_NORMAL_DATA"
					" (%ld bytes at offset %ld)\n",
					item->bytes, item->offset);
				c = stp_session_context(item->data_index);
				data = &c->transport_data;
				read_ptr = (data->print_buf
					    + _STP_D_T_PRINT_NORM(item->offset));
				if (_stp_write_retry(out_fd, read_ptr, item->bytes) < 0)
					_stp_transport_err(
						"couldn't write %ld bytes data: %s\n",
						(long)item->bytes, strerror(errno));

				pthread_mutex_lock(&(data->print_mutex));

				// Now we need to update the read
				// pointer, using the data_index we
				// received. Note that we're doing
				// this with or without that context
				// locked, but the print_mutex is
				// locked.
				data->read_offset = _STP_D_T_PRINT_ADD(item->offset, item->bytes);

				// Signal more bytes available to any waiters.
				pthread_cond_signal(&(data->print_space_avail));
				pthread_mutex_unlock(&(data->print_mutex));

				_stp_transport_debug(
					"STP_DYN_NORMAL_DATA flushed,"
					" read_offset %ld, write_offset %ld)\n",
					data->read_offset, data->write_offset);
				break;

			case STP_DYN_EXIT:
				_stp_transport_debug("STP_DYN_EXIT\n");
				stopping = 1;
				break;

			case STP_DYN_REQUEST_EXIT:
				_stp_transport_debug("STP_DYN_REQUEST_EXIT\n");
				__stp_d_t_request_exit();
				break;

			default:
				if (! (item->type & STP_DYN_OOB_DATA_MASK)) {
					_stp_transport_err(
						"Error - unknown item type"
						" %d\n", item->type);
				}
				break;
			}
		}

		// We're now finished with the read queue. Clear it
		// out.
		q->items = 0;
	}
	return NULL;
}

static int _stp_ctl_send(int type, void *data, unsigned len)
{
	_stp_transport_debug("type 0x%x data %p len %d\n",
			type, data, len);

	// This thread should already have a context structure.
        struct context* c = _stp_runtime_get_context();
	if (c == NULL)
		return EINVAL;

	// Currently, we're only handling 'STP_SYSTEM' control
	// messages, converting it to a STP_DYN_SYSTEM message.
	if (type != STP_SYSTEM)
		return 0;

	char *buffer = _stp_dyninst_transport_log_buffer();
	if (buffer == NULL)
		return 0;

	memcpy(buffer, data, len);
	size_t offset = buffer - c->transport_data.log_buf;
	__stp_dyninst_transport_queue_add(STP_DYN_SYSTEM,
					  c->data_index, offset, len);
	return len;
}

static void _stp_dyninst_transport_signal_exit(void)
{
	__stp_dyninst_transport_queue_add(STP_DYN_EXIT, 0, 0, 0);
}

static void _stp_dyninst_transport_request_exit(void)
{
	__stp_dyninst_transport_queue_add(STP_DYN_REQUEST_EXIT, 0, 0, 0);
}

static int _stp_dyninst_transport_session_init(void)
{
	int rc;

	// Set up the transport session data.
	struct _stp_transport_session_data *sess_data = stp_transport_data();
	if (sess_data != NULL) {
		rc = stp_pthread_mutex_init_shared(&(sess_data->queue_mutex));
		if (rc != 0) {
			_stp_error("transport queue mutex initialization"
				   " failed");
			return rc;
		}
		rc = stp_pthread_cond_init_shared(&(sess_data->queue_space_avail));
		if (rc != 0) {
			_stp_error("transport queue space avail cond variable"
				   " initialization failed");
			return rc;
		}
		rc = stp_pthread_cond_init_shared(&(sess_data->queue_data_avail));
		if (rc != 0) {
			_stp_error("transport queue empty cond variable"
				   " initialization failed");
			return rc;
		}
	}

	// Set up each context's transport data.
	int i;
	for_each_possible_cpu(i) {
		struct context *c;
		struct _stp_transport_context_data *data;
		c = stp_session_context(i);
		if (c == NULL)
			continue;
		data = &c->transport_data;
		rc = stp_pthread_mutex_init_shared(&(data->print_mutex));
		if (rc != 0) {
			_stp_error("transport mutex initialization failed");
			return rc;
		}

		rc = stp_pthread_cond_init_shared(&(data->print_space_avail));
		if (rc != 0) {
			_stp_error("transport cond variable initialization failed");
			return rc;
		}

		rc = stp_pthread_mutex_init_shared(&(data->log_mutex));
		if (rc != 0) {
			_stp_error("transport log mutex initialization failed");
			return rc;
		}

		rc = stp_pthread_cond_init_shared(&(data->log_space_avail));
		if (rc != 0) {
			_stp_error("transport log cond variable initialization failed");
			return rc;
		}
	}

	return 0;
}

static int _stp_dyninst_transport_session_start(void)
{
	int rc;

	// Start the thread.
	rc = pthread_create(&_stp_transport_thread, NULL,
			    &_stp_dyninst_transport_thread_func, NULL);
	if (rc != 0) {
		_stp_error("transport thread creation failed (%d)", rc);
		return rc;
	}
	_stp_transport_thread_started = 1;
	return 0;
}

static int
_stp_dyninst_transport_write_oob_data(char *buffer, size_t bytes)
{
	// This thread should already have a context structure.
        struct context* c = _stp_runtime_get_context();
	if (c == NULL)
		return EINVAL;

	size_t offset = buffer - c->transport_data.log_buf;
	__stp_dyninst_transport_queue_add(STP_DYN_OOB_DATA,
					  c->data_index, offset, bytes);
	return 0;
}

static int _stp_dyninst_transport_write(void)
{
	// This thread should already have a context structure.
        struct context* c = _stp_runtime_get_context();
	if (c == NULL)
		return 0;
	struct _stp_transport_context_data *data = &c->transport_data;
	size_t bytes = data->write_bytes;

	if (bytes == 0)
		return 0;

	// This should be thread-safe without using any additional
	// locking. This probe is the only one using this context and
	// the transport thread (the consumer) only writes to
	// 'read_offset'. Any concurrent-running probe will be using a
	// different context.
	_stp_transport_debug(
		"read_offset %ld, write_offset %ld, write_bytes %ld\n",
		data->read_offset, data->write_offset, data->write_bytes);

	// Notice we're not normalizing 'write_offset'. The consumer
	// thread needs "raw" offsets.
	size_t saved_write_offset = data->write_offset;
	data->write_bytes = 0;

	// Note that if we're writing all remaining bytes in the
	// buffer, it can wrap (but only to either "high" or "low"
	// 0).
	data->write_offset = _STP_D_T_PRINT_ADD(data->write_offset, bytes);

	__stp_dyninst_transport_queue_add(STP_DYN_NORMAL_DATA,
					  c->data_index,
					  saved_write_offset, bytes);
	return 0;
}

static void _stp_dyninst_transport_shutdown(void)
{
	// If we started the thread, tear everything down.
	if (_stp_transport_thread_started != 1) {
		return;
	}

	// Signal the thread to stop.
	_stp_dyninst_transport_signal_exit();

	// Wait for thread to quit...
	pthread_join(_stp_transport_thread, NULL);
	_stp_transport_thread_started = 0;

	// Tear down the transport session data.
	struct _stp_transport_session_data *sess_data = stp_transport_data();
	if (sess_data != NULL) {
		pthread_mutex_destroy(&(sess_data->queue_mutex));
		pthread_cond_destroy(&(sess_data->queue_space_avail));
		pthread_cond_destroy(&(sess_data->queue_data_avail));
	}

	// Tear down each context's transport data.
	int i;
	for_each_possible_cpu(i) {
		struct context *c;
		struct _stp_transport_context_data *data;
		c = stp_session_context(i);
		if (c == NULL)
			continue;
		data = &c->transport_data;
		pthread_mutex_destroy(&(data->print_mutex));
		pthread_cond_destroy(&(data->print_space_avail));
		pthread_mutex_destroy(&(data->log_mutex));
		pthread_cond_destroy(&(data->log_space_avail));
	}
}

static int
_stp_dyninst_transport_log_buffer_full(struct _stp_transport_context_data *data)
{
	// This inverts the most significant bit of 'log_start' before
	// comparison.
	return (data->log_end == (data->log_start ^ _STP_LOG_BUF_ENTRIES));
}


static char *_stp_dyninst_transport_log_buffer(void)
{
	// This thread should already have a context structure.
        struct context* c = _stp_runtime_get_context();
	if (c == NULL)
		return NULL;

	// Note that the context structure is locked, so only one
	// probe at a time can be operating on it.
	struct _stp_transport_context_data *data = &c->transport_data;

	// If there isn't an available log buffer, wait.
	if (_stp_dyninst_transport_log_buffer_full(data)) {
		pthread_mutex_lock(&(data->log_mutex));
		while (_stp_dyninst_transport_log_buffer_full(data)) {
			pthread_cond_wait(&(data->log_space_avail),
					  &(data->log_mutex));
		}
		pthread_mutex_unlock(&(data->log_mutex));
	}

	// Note that we're taking 'log_end' and normalizing it to start
	// at 0 to get the proper entry number. We then multiply it by
	// STP_LOG_BUF_LEN to find the proper buffer offset.
	//
	// Every "allocation" here is done in STP_LOG_BUF_LEN-sized
	// chunks.
	char *ptr = &data->log_buf[_STP_D_T_LOG_NORM(data->log_end)
				   * STP_LOG_BUF_LEN];

	// Increment 'log_end'.
	data->log_end = _STP_D_T_LOG_INC(data->log_end);
	return ptr;
}

static size_t
__stp_d_t_space_before(struct _stp_transport_context_data *data,
		       size_t read_offset)
{
	// If the offsets have differing most significant bits, then
	// the write offset has wrapped, so there isn't any available
	// space before the write offset.
	if ((read_offset & _STP_DYNINST_BUFFER_SIZE)
	    != (data->write_offset & _STP_DYNINST_BUFFER_SIZE)) {
		return 0;
	}

	return (_STP_D_T_PRINT_NORM(read_offset));
}

static size_t
__stp_d_t_space_after(struct _stp_transport_context_data *data,
		      size_t read_offset)
{
	// We have to worry about wraparound here, in the case of a
	// full buffer.
	size_t write_end_offset = _STP_D_T_PRINT_ADD(data->write_offset,
						     data->write_bytes);

	// If the offsets have differing most significant bits, then
	// the write offset has wrapped, so the only available space
	// after the write offset is between the (normalized) write
	// offset and the (normalized) read offset.
	if ((read_offset & _STP_DYNINST_BUFFER_SIZE)
	    != (write_end_offset & _STP_DYNINST_BUFFER_SIZE)) {
		return (_STP_D_T_PRINT_NORM(read_offset)
			- _STP_D_T_PRINT_NORM(write_end_offset));
	}

	return (_STP_DYNINST_BUFFER_SIZE
		- _STP_D_T_PRINT_NORM(write_end_offset));
}

static void *_stp_dyninst_transport_reserve_bytes(int numbytes)
{
	void *ret;

	// This thread should already have a context structure.
        struct context* c = _stp_runtime_get_context();
	if (c == NULL) {
		_stp_transport_debug("NULL context!\n");
		return NULL;
	}

	struct _stp_transport_context_data *data = &c->transport_data;
	size_t space_before, space_after, read_offset;

recheck:
	pthread_mutex_lock(&(data->print_mutex));

	// If the buffer is empty, reset everything to the
	// beginning. This cuts down on fragmentation.
	if (data->write_bytes == 0 && data->read_offset == data->write_offset
	    && data->read_offset != 0) {
		data->read_offset = 0;
		data->write_offset = 0;
	}
	// We cache the read_offset value to get a consistent view of
	// the buffer (between calls to get the space before/after).
        read_offset = data->read_offset;
	pthread_mutex_unlock(&(data->print_mutex));

	space_before = __stp_d_t_space_before(data, read_offset);
	space_after = __stp_d_t_space_after(data, read_offset);

	// If we don't have enough space, try to get more space by
	// flushing and/or waiting.
	if (space_before < numbytes && space_after < numbytes) {
		// First, lock the mutex.
		pthread_mutex_lock(&(data->print_mutex));

		// There is a race condition here. We've checked for
		// available free space, then locked the mutex. It is
		// possible for more free space to have become
		// available between the time we checked and the time
		// we locked the mutex. Recheck the available free
		// space.
		read_offset = data->read_offset;
		space_before = __stp_d_t_space_before(data, read_offset);
		space_after = __stp_d_t_space_after(data, read_offset);

		// If we still don't have enough space and we have
		// data we haven't flushed, go ahead and flush to free
		// up space.
		if (space_before < numbytes && space_after < numbytes
		    && data->write_bytes != 0) {
			// Flush the buffer. We have to do this while
			// the mutex is locked, so that we can't miss
			// the condition change. (If we did flush
			// without the mutex locked, it would be
			// possible for the consumer thread to signal
			// the condition variable before we were
			// waiting on it.)
			_stp_dyninst_transport_write();

			// Mutex is locked. It is automatically
			// unlocked while we are waiting.
			pthread_cond_wait(&(data->print_space_avail),
					  &(data->print_mutex));
			// Mutex is locked again.

			// Recheck available free space.
			read_offset = data->read_offset;
			space_before = __stp_d_t_space_before(data,
							      read_offset);
			space_after = __stp_d_t_space_after(data, read_offset);
		}

		// If we don't have enough bytes available, do a timed
		// wait for more bytes to become available. This might
		// fail if there isn't anything in the queue for this
		// context structure.
		if (space_before < numbytes && space_after < numbytes) {
			_stp_transport_debug(
				"waiting for more space, numbytes %d,"
				" before %ld, after %ld\n",
				numbytes, space_before, space_after);

			// Setup a timeout for
			// STP_DYNINST_TIMEOUT_SECS seconds into the
			// future.
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += STP_DYNINST_TIMEOUT_SECS;

			// Mutex is locked. It is automatically
			// unlocked while we are waiting.
			pthread_cond_timedwait(&(data->print_space_avail),
					       &(data->print_mutex),
					       &ts);
			// When pthread_cond_timedwait() returns, the
			// mutex has been (re)locked.

			// Now see if we've got more bytes available.
			read_offset = data->read_offset;
			space_before = __stp_d_t_space_before(data,
							      read_offset);
			space_after = __stp_d_t_space_after(data, read_offset);
		}
		// We're finished with the mutex.
		pthread_mutex_unlock(&(data->print_mutex));

		// If we *still* don't have enough space available,
		// quit. We've done all we can do.
		if (space_before < numbytes && space_after < numbytes) {
			_stp_transport_debug(
				"not enough space available,"
				" numbytes %d, before %ld, after %ld,"
				" read_offset %ld, write_offset %ld\n",
				numbytes, space_before, space_after,
				read_offset, data->write_offset);
			return NULL;
		}
	}

	// OK, now we have enough space, either before or after the
	// current write offset.
	//
	// We prefer using the size after the current write, which
	// will help keep writes contiguous.
	if (space_after >= numbytes) {
		ret = (data->print_buf
		       + _STP_D_T_PRINT_NORM(data->write_offset)
		       + data->write_bytes);
		data->write_bytes += numbytes;
		_stp_transport_debug(
			"reserve %d bytes after, bytes available"
			" (%ld, %ld) read_offset %ld, write_offset %ld,"
			" write_bytes %ld\n",
			numbytes, space_before, space_after, data->read_offset,
			data->write_offset, data->write_bytes);
		return ret;
	}

	// OK, now we know we need to use the space before the write
	// offset. If we've got existing bytes that haven't been
	// flushed, flush them now.
	if (data->write_bytes != 0) {
		_stp_dyninst_transport_write();
		// Flushing the buffer updates the write_offset, which
		// could have caused it to wrap. Start all over.
		_stp_transport_debug(
			"rechecking available bytes after a flush...\n");
		goto recheck;
	}

	// Wrap the offset around by inverting the most significant
	// bit, then clearing out the lower bits.
	data->write_offset = ((data->write_offset ^ _STP_DYNINST_BUFFER_SIZE)
			      & _STP_DYNINST_BUFFER_SIZE);
	ret = data->print_buf;
	data->write_bytes += numbytes;
	_stp_transport_debug(
		"reserve %d bytes before, bytes available"
		" (%ld, %ld) read_offset %ld, write_offset %ld,"
		" write_bytes %ld\n",
		numbytes, space_before, space_after, data->read_offset,
		data->write_offset, data->write_bytes);
	return ret;
}

static void _stp_dyninst_transport_unreserve_bytes(int numbytes)
{
	// This thread should already have a context structure.
        struct context* c = _stp_runtime_get_context();
	if (c == NULL)
		return;

	struct _stp_transport_context_data *data = &c->transport_data;
	if (unlikely(numbytes <= 0 || numbytes > data->write_bytes))
		return;

	data->write_bytes -= numbytes;
}
#endif /* _STAPDYN_TRANSPORT_C_ */
