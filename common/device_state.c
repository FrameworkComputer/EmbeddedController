/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "device_state.h"
#include "hooks.h"

static int enabled = 1;

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

	if (!enabled)
		return;

	for (i = 0; i < DEVICE_COUNT; i++)
		board_update_device_state(i);
}
DECLARE_HOOK(HOOK_SECOND, check_device_state, HOOK_PRIO_DEFAULT);

static int device_has_interrupts(enum device_type device)
{
	return (device_states[device].deferred &&
		device_states[device].detect_on != GPIO_COUNT &&
		device_states[device].detect_off != GPIO_COUNT);
}

static void disable_interrupts(enum device_type device)
{
	if (!device_has_interrupts(device))
		return;

	/* Cancel any deferred callbacks */
	hook_call_deferred(device_states[device].deferred, -1);

	/* Disable gpio interrupts */
	gpio_disable_interrupt(device_states[device].detect_on);
	gpio_disable_interrupt(device_states[device].detect_off);
}

static void enable_interrupts(enum device_type device)
{
	if (!device_has_interrupts(device))
		return;

	/* Enable gpio interrupts */
	gpio_enable_interrupt(device_states[device].detect_on);
	gpio_enable_interrupt(device_states[device].detect_off);
}

void device_detect_state_enable(int enable)
{
	int i;

	enabled = enable;
	for (i = 0; i < DEVICE_COUNT; i++) {
		if (enabled) {
			enable_interrupts(i);
			board_update_device_state(i);
		} else {
			disable_interrupts(i);
			device_set_state(i, DEVICE_STATE_UNKNOWN);
		}
	}
}

static void print_state(const char *name, enum device_state state)
{
	ccprintf("%-9s %s\n", name, state == DEVICE_STATE_ON ? "on" :
		 state == DEVICE_STATE_OFF ? "off" : "unknown");
}

static int command_devices(int argc, char **argv)
{
	int i;

	if (!enabled)
		ccprintf("Device monitoring disabled\n");
	else
		for (i = 0; i < DEVICE_COUNT; i++)
			print_state(device_states[i].name,
				    device_states[i].state);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(devices, command_devices,
	"",
	"Get the device states",
	NULL);
