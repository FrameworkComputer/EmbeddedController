/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "device_state.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/**
 * Return text description for a state
 *
 * @param state		State
 * @return String describing that state
 */
static const char *state_desc(enum device_state state)
{
	return state == DEVICE_STATE_ON ? "on" :
			state == DEVICE_STATE_OFF ? "off" : "unknown";
}

enum device_state device_get_state(enum device_type device)
{
	return device_states[device].state;
}

int device_set_state(enum device_type device, enum device_state state)
{
	struct device_config *dc = device_states + device;

	/*
	 * It'd be handy for debugging if we could print to the console when
	 * device_set_state() is called.  But unfortunately, it'll be called a
	 * LOT when debouncing UART activity on DETECT_EC or DETECT_AP.  So
	 * only print when the last known state changes below.
	 */

	dc->state = state;

	if (state != DEVICE_STATE_UNKNOWN && dc->last_known_state != state) {
		dc->last_known_state = state;
		CPRINTS("DEV %s -> %s", dc->name, state_desc(state));
		return 1;
	}

	return 0;
}

/**
 * Periodic check of device states.
 *
 * The board does all the work.
 *
 * Note that device states can change outside of this context as well, for
 * example, from a GPIO interrupt handler.
 */
static void check_device_state(void)
{
	int i;

	for (i = 0; i < DEVICE_COUNT; i++)
		board_update_device_state(i);
}
DECLARE_HOOK(HOOK_SECOND, check_device_state, HOOK_PRIO_DEFAULT);

static int command_devices(int argc, char **argv)
{
	const struct device_config *dc = device_states;
	int i;

	ccprintf("Device    State   LastKnown\n");

	for (i = 0; i < DEVICE_COUNT; i++, dc++)
		ccprintf("%-9s %-7s %s\n", dc->name, state_desc(dc->state),
			 state_desc(dc->last_known_state));

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(devices, command_devices,
			     "",
			     "Get the device states");
