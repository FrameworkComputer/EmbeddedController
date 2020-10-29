/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TASK_ID_H
#define __CROS_EC_SHIMMED_TASK_ID_H

/* Task identifier (8 bits) */
typedef uint8_t task_id_t;

/* Include the shimmed tasks for the project/board */
#ifdef CONFIG_SHIMMED_TASKS
#include "shimmed_tasks.h"
#else
#define CROS_EC_TASK_LIST
#endif

/* Define the task_ids globally for all shimmed platform/ec code to use */
#define CROS_EC_TASK(name, ...) TASK_ID_##name,
enum {
	TASK_ID_IDLE = -1, /* We don't shim the idle task */
	CROS_EC_TASK_LIST
	TASK_ID_COUNT,
	TASK_ID_INVALID = 0xff, /* Unable to find the task */
};
#undef CROS_EC_TASK

#endif /* __CROS_EC_SHIMMED_TASK_ID_H */
