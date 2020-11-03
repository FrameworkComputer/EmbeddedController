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
#define HAS_TASK_EXAMPLE 1

/*
 * Highest priority on bottom -- same as in platform/ec. List of CROS_EC_TASK
 * items. See CONFIG_TASK_LIST in platform/ec's config.h for more informaiton
 */
#define CROS_EC_TASK_LIST  \
	CROS_EC_TASK(EXAMPLE, example_entry, 0, 512)


/* TODO remove once we have first shimmed volteer task */
static inline void example_entry(void *p) {}

#endif /* __CROS_EC_SHIMMED_TASKS_H */
