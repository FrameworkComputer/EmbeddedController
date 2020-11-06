/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_EC_TASKS_H
#define __CROS_EC_EC_TASKS_H

/** Starts all of the shimmed EC tasks. Requires CONFIG_SHIMMED_TASKS=y. */
void start_ec_tasks(void);

#endif  /* __CROS_EC_EC_TASKS_H */
