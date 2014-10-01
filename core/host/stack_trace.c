/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "host_task.h"
#include "host_test.h"
#include "timer.h"

#define SIGNAL_TRACE_DUMP SIGTERM
#define MAX_TRACE 30
/*
 * When trace dump is requested from signal handler, skip:
 *   _task_dump_trace_impl
 *   _task_dump_trace_dispath
 *   A function in libc
 */
#define SIGNAL_TRACE_OFFSET 3
/*
 * When trace dump is requested from task_dump_trace(), skip:
 *   task_dump_trace
 *   _task_dump_trace_impl
 */
#define DIRECT_TRACE_OFFSET 2

static pthread_t main_thread;

static void __attribute__((noinline)) _task_dump_trace_impl(int offset)
{
	void *trace[MAX_TRACE];
	size_t sz;
	char **messages;
	char buf[256];
	FILE *file;
	int i, nb;

	sz = backtrace(trace, MAX_TRACE);
	messages = backtrace_symbols(trace + offset, sz - offset);

	for (i = 0; i < sz - offset; ++i) {
		fprintf(stderr, "#%-2d %s\n", i, messages[i]);
		sprintf(buf, "addr2line %p -e %s",
			trace[i + offset], __get_prog_name());
		file = popen(buf, "r");
		if (file) {
			nb = fread(buf, 1, sizeof(buf) - 1, file);
			buf[nb] = '\0';
			fprintf(stderr, "    %s", buf);
			pclose(file);
		}
	}
	fflush(stderr);
	free(messages);
}

void __attribute__((noinline)) task_dump_trace(void)
{
	_task_dump_trace_impl(DIRECT_TRACE_OFFSET);
}

static void __attribute__((noinline)) _task_dump_trace_dispatch(int sig)
{
	int need_dispatch = 1;
	task_id_t running = task_get_running();

	if (!pthread_equal(pthread_self(), main_thread)) {
		need_dispatch = 0;
	} else if (!task_start_called()) {
		fprintf(stderr, "Stack trace of main thread:\n");
		need_dispatch = 0;
	} else if (in_interrupt_context()) {
		fprintf(stderr, "Stack trace of ISR:\n");
	} else {
		fprintf(stderr, "Stack trace of task %d (%s):\n",
				running, task_get_name(running));
	}

	if (need_dispatch) {
		pthread_kill(task_get_thread(running), SIGNAL_TRACE_DUMP);
	} else {
		_task_dump_trace_impl(SIGNAL_TRACE_OFFSET);
		exit(1);
	}
}

void task_register_tracedump(void)
{
	/* Trace dumper MUST be registered from main thread */
	main_thread = pthread_self();
	signal(SIGNAL_TRACE_DUMP, _task_dump_trace_dispatch);
}
