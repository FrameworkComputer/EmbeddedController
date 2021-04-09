/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TEST_TASKS_H
#define __CROS_EC_SHIMMED_TEST_TASKS_H

/* Highest priority on bottom same as in platform/ec */
#define CROS_EC_TASK_LIST                                                      \
	CROS_EC_TASK(HOOKS, hook_task, 0, CONFIG_TASK_HOOKS_STACK_SIZE)        \
	CROS_EC_TASK(CHG_RAMP, chg_ramp_task, 0,                               \
		     CONFIG_TASK_CHG_RAMP_STACK_SIZE)                          \
	CROS_EC_TASK(USB_CHG_P0, usb_charger_task, 0,                          \
		     CONFIG_TASK_USB_CHG_STACK_SIZE)                           \
	CROS_EC_TASK(CHARGER, charger_task, 0, CONFIG_TASK_CHARGER_STACK_SIZE) \
	CROS_EC_TASK(HOSTCMD, host_command_task, 0,                            \
		     CONFIG_TASK_HOSTCMD_STACK_SIZE)                           \
	CROS_EC_TASK(PD_C0, pd_task, 0, CONFIG_TASK_PD_STACK_SIZE)
#endif /* __CROS_EC_SHIMMED_TEST_TASKS_H */
