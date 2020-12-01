/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TASK_ID_H
#define __CROS_EC_SHIMMED_TASK_ID_H

#include "common.h"

/* Task identifier (8 bits) */
typedef uint8_t task_id_t;

/*
 * Highest priority on bottom -- same as in platform/ec. List of CROS_EC_TASK
 * items. See CONFIG_TASK_LIST in platform/ec's config.h for more information.
 * This will only automatically get generated if CONFIG_ZTEST is not defined.
 * Unit tests must define their own tasks.
 */
#ifndef CONFIG_ZTEST
#define CROS_EC_TASK_LIST                                                 \
	COND_CODE_1(HAS_TASK_CHIPSET,                                     \
		     (CROS_EC_TASK(CHIPSET, chipset_task, 0,              \
				   CONFIG_TASK_CHIPSET_STACK_SIZE)), ())  \
	COND_CODE_1(HAS_TASK_HOSTCMD,                                     \
		     (CROS_EC_TASK(HOSTCMD, host_command_task, 0,         \
				   CONFIG_TASK_HOSTCMD_STACK_SIZE)), ())  \
	COND_CODE_1(HAS_TASK_KEYPROTO,                                    \
		     (CROS_EC_TASK(KEYPROTO, keyboard_protocol_task, 0,   \
				   CONFIG_TASK_KEYPROTO_STACK_SIZE)), ()) \
	COND_CODE_1(HAS_TASK_POWERBTN,                                    \
		     (CROS_EC_TASK(POWERBTN, power_button_task, 0,        \
				   CONFIG_TASK_POWERBTN_STACK_SIZE)), ()) \
	COND_CODE_1(HAS_TASK_KEYSCAN,                                     \
		     (CROS_EC_TASK(KEYSCAN, keyboard_scan_task, 0,        \
				   CONFIG_TASK_KEYSCAN_STACK_SIZE)), ())
#endif /* !CONFIG_ZTEST */

#ifndef CROS_EC_TASK_LIST
#define CROS_EC_TASK_LIST
#endif /* CROS_EC_TASK_LIST */

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
