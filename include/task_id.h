/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* define the task identifier of all compiled tasks */

#ifndef __TASK_ID_H
#define __TASK_ID_H

/* define the name of the header containing the list of tasks */
#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)
#define TASK_LIST STRINGIFY(TASKFILE)

/* Task identifier (8 bits) */
typedef uint8_t task_id_t;

/**
 * enumerate all tasks in the priority order
 *
 * the identifier of a task can be retrieved using the following constant:
 * TASK_ID_<taskname> where <taskname> is the first parameter passed to the
 * TASK macro in the TASK_LIST file.
 */
#define TASK(n, r, d, s) TASK_ID_##n,
#include TASK_LIST
enum {
	TASK_ID_IDLE,
	/* CONFIG_TASK_LIST is a macro coming from the TASK_LIST file */
	CONFIG_TASK_LIST
	/* Number of tasks */
	TASK_ID_COUNT,
	/* Special task identifiers */
	TASK_ID_INVALID = 0xff  /* unable to find the task */
};
#undef TASK

#endif  /* __TASK_ID_H */
