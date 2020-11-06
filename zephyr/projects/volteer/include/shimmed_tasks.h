/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TASKS_H
#define __CROS_EC_SHIMMED_TASKS_H

/*
 * Manually define these HAS_TASK_* defines. There is a build time assert
 * to at least verify we have the minimum set defined correctly.
 */
#define HAS_TASK_CHIPSET 1
#define HAS_TASK_HOSTCMD 1
#define HAS_TASK_KEYPROTO 1
#define HAS_TASK_KEYSCAN 1
#define HAS_TASK_POWERBTN 1

/*
 * Highest priority on bottom -- same as in platform/ec. List of CROS_EC_TASK
 * items. See CONFIG_TASK_LIST in platform/ec's config.h for more informaiton
 */
#define CROS_EC_TASK_LIST                                      \
	CROS_EC_TASK(CHIPSET, chipset_task, 0, 512)            \
	CROS_EC_TASK(HOSTCMD, host_command_task, 0, 512)       \
	CROS_EC_TASK(KEYPROTO, keyboard_protocol_task, 0, 512) \
	CROS_EC_TASK(POWERBTN, power_button_task, 0, 512)      \
	CROS_EC_TASK(KEYSCAN, keyboard_scan_task, 0, 512)

#endif /* __CROS_EC_SHIMMED_TASKS_H */
