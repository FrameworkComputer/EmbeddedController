/* Copyright 2011 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* define the task identifier of all compiled tasks */

#ifndef __CROS_EC_TASK_ID_H
#define __CROS_EC_TASK_ID_H

/* For Zephyr builds just used shimmed tasks ids, otherwise use platform/ec's */
#ifdef CONFIG_ZEPHYR
#include "shimmed_task_id.h"
#else

#include "config.h"
#include "task_filter.h"

/* define the name of the header containing the list of tasks */
#define STRINGIFY0(name) #name
#define STRINGIFY(name) STRINGIFY0(name)
#define CTS_TASK_LIST STRINGIFY(CTS_TASKFILE)
#define TEST_TASK_LIST STRINGIFY(TEST_TASKFILE)
#define BOARD_TASK_LIST STRINGIFY(BOARD_TASKFILE)

#include BOARD_TASK_LIST
#ifdef CTS_MODULE
#include CTS_TASK_LIST
#endif
#ifdef TEST_BUILD
#include TEST_TASK_LIST
#endif

/* Task identifier (8 bits) */
typedef uint8_t task_id_t;

/**
 * enumerate all tasks in the priority order
 *
 * the identifier of a task can be retrieved using the following constant:
 * TASK_ID_<taskname> where <taskname> is the first parameter passed to the
 * TASK macro in the TASK_LIST file.
 */
#define TASK(n, ...) TASK_ID_##n,
enum {
	TASK_ID_IDLE,
	/* CONFIG_TASK_LIST is a macro coming from the BOARD_TASK_LIST file */
	CONFIG_TASK_LIST
		/* CONFIG_TEST_TASK_LIST is a macro from the TEST_TASK_LIST file
		 */
		CONFIG_TEST_TASK_LIST
			/* For CTS tasks */
			CONFIG_CTS_TASK_LIST
#ifdef EMU_BUILD
				TASK_ID_TEST_RUNNER,
#endif
	/* Number of tasks */
	TASK_ID_COUNT,
/* Special task identifiers */
#ifdef EMU_BUILD
	TASK_ID_INT_GEN = 0xfe, /* interrupt generator */
#endif
	TASK_ID_INVALID = 0xff, /* unable to find the task */
};
#undef TASK

#endif /* CONFIG_ZEPHYR */
#endif /* __CROS_EC_TASK_ID_H */
