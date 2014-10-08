/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef _SIMULATION_H
#define _SIMULATION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lb_common.h"
#include "lightbar.h"

/* Functions specific to our simulation environment */
void *entry_windows(void *);
void *entry_input(void *);
void *entry_lightbar(void *);
void init_windows(void);
int lb_read_params_from_file(const char *filename,
			     struct lightbar_params_v1 *p);
int lb_load_program(const char *filename, struct lightbar_program *prog);
/* Interfaces to the EC code that we're encapsulating */
void lightbar_task(void);
int fake_consolecmd_lightbar(int argc, char *argv[]);

/* EC-specific configuration */
#undef DEMO_MODE_DEFAULT
#define DEMO_MODE_DEFAULT 1
#ifndef CONFIG_CONSOLE_CMDHELP
#define CONFIG_CONSOLE_CMDHELP
#endif
#ifndef CONFIG_LIGHTBAR_POWER_RAILS
#define CONFIG_LIGHTBAR_POWER_RAILS
#endif


/* Stuff that's too interleaved with the rest of the EC to just include */

/* Test an important condition at compile time, not run time */
#define _BA1_(cond, line) \
	extern int __build_assertion_ ## line[1 - 2*!(cond)]	\
	__attribute__ ((unused))
#define _BA0_(c, x) _BA1_(c, x)
#define BUILD_ASSERT(cond) _BA0_(cond, __LINE__)

/* Number of elements in an array */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Non-standard standard library functions */
void cprintf(int zero, const char *fmt, ...);
void cprints(int zero, const char *fmt, ...);
#define ccprintf(fmt...) cprintf(0, fmt)
#define strtoi strtol

/* Task events */
#define TASK_EVENT_CUSTOM(x)    (x & 0x0fffffff)
#define TASK_EVENT_I2C_IDLE     0x10000000
#define TASK_EVENT_WAKE         0x20000000
#define TASK_EVENT_MUTEX        0x40000000
#define TASK_EVENT_TIMER        0x80000000

/* Time units in usecs */
#define MSEC         1000
#define SECOND    1000000

#define TASK_ID_LIGHTBAR 0
#define CC_LIGHTBAR 0

/* Other definitions and structs */
#define EC_SUCCESS 0
#define EC_ERROR_INVAL 5
#define EC_ERROR_PARAM1 11
#define EC_ERROR_PARAM2 12

typedef int task_id_t;

typedef union {
	uint64_t val;
	struct {
		uint32_t lo;
		uint32_t hi;
	} le /* little endian words */;
} timestamp_t;

struct host_cmd_handler_args {
	const void *params;
	void *response;
	int response_size;
};

/* EC functions that we have to provide */
uint32_t task_wait_event(int timeout_us);
uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait_for_reply);
timestamp_t get_time(void);
int system_add_jump_tag(uint16_t tag, int version, int size, const void *data);
uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size);

/* Export unused static functions to avoid compiler warnings. */
#define DECLARE_HOOK(X, fn, Y) \
	void fake_hook_##fn(void) { fn(); }

#define DECLARE_HOST_COMMAND(X, fn, Y) \
	int fake_hostcmd_##fn(struct host_cmd_handler_args *args) \
	{ return fn(args); }

#define DECLARE_CONSOLE_COMMAND(X, fn, Y...) \
	int fake_consolecmd_##X(int argc, char *argv[]) \
	{ return fn(argc, argv); }

#endif	/* _SIMULATION_H */
