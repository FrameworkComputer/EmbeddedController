/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset module for emulator */

#include <stdio.h>
#include "chipset.h"
#include "common.h"
#include "task.h"

test_mockable void chipset_reset(int cold_reset)
{
	fprintf(stderr, "Chipset reset!\n");
}

test_mockable void chipset_force_shutdown(void)
{
	/* Do nothing */
}

#ifdef HAS_TASK_CHIPSET
test_mockable int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_SOFT_OFF;
}

test_mockable void chipset_task(void)
{
	while (1)
		task_wait_event(-1);
}
#endif
