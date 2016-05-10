/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "device_state.h"
#include "hooks.h"

int device_get_state(enum device_type device)
{
	return device_states[device].state;
}

void device_set_state(enum device_type device, enum device_state state)
{
	if (device_states[device].state == state)
		return;

	device_states[device].state = state;
}

static void check_device_state(void)
{
	int i;

	for (i = 0; i < DEVICE_COUNT; i++)
		board_update_device_state(i);
}
DECLARE_HOOK(HOOK_SECOND, check_device_state, HOOK_PRIO_DEFAULT);
