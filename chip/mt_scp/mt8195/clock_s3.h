/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CLOCK_S3_H
#define __CLOCK_S3_H

enum scp_clock_source {
	SCP_CLK_SYSTEM,
	SCP_CLK_32K,
	SCP_CLK_ULPOSC1,
	SCP_CLK_ULPOSC2_LOW_SPEED,
	SCP_CLK_ULPOSC2_HIGH_SPEED,
};

void clock_select_clock(enum scp_clock_source src);

#define TASK_EVENT_SUSPEND TASK_EVENT_CUSTOM_BIT(4)
#define TASK_EVENT_RESUME TASK_EVENT_CUSTOM_BIT(5)

#endif /* __CLOCK_S3_H */
