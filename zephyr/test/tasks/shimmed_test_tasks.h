/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TEST_TASKS_H
#define __CROS_EC_SHIMMED_TEST_TASKS_H

/*
 * Manually define these HAS_TASK_* defines. There is a build time assert
 * to at least verify we have the minimum set defined correctly. */
#define HAS_TASK_TASK_1 1
#define HAS_TASK_TASK_2 1
#define HAS_TASK_TASK_3 1

/* Highest priority on bottom same as in platform/ec */
#define CROS_EC_TASK_LIST                               \
	CROS_EC_TASK(TASK_1, task1_entry, 0, 512, 2, 0) \
	CROS_EC_TASK(TASK_2, task2_entry, 0, 512, 1, 0) \
	CROS_EC_TASK(TASK_3, task3_entry, 0, 512, 0, 0)

#endif /* __CROS_EC_SHIMMED_TEST_TASKS_H */
