/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
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

	if (state != DEVICE_STATE_UNKNOWN)
		device_states[device].last_known_state = state;

	device_states[device].state = state;
}

static void check_device_state(void)
{
	int i;

	for (i = 0; i < DEVICE_COUNT; i++)
		board_update_device_state(i);
}
DECLARE_HOOK(HOOK_SECOND, check_device_state, HOOK_PRIO_DEFAULT);

static void print_state(const char *name, enum device_state state)
{
	ccprintf("%-9s %s\n", name, state == DEVICE_STATE_ON ? "on" :
		 state == DEVICE_STATE_OFF ? "off" : "unknown");
}

static int command_devices(int argc, char **argv)
{
	int i;

	for (i = 0; i < DEVICE_COUNT; i++)
		print_state(device_states[i].name,
			    device_states[i].state);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(devices, command_devices,
			     "",
			     "Get the device states");
