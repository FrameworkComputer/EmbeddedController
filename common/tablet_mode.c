/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "tablet_mode.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_LID, format, ## args)

/* 1: in tablet mode; 0: notebook mode; -1: uninitialized  */
static int tablet_mode = -1;

/* 1: hall sensor is reporting 360 degrees. */
static int hall_sensor_at_360;

/*
 * 1: all calls to tablet_set_mode are ignored and tablet_mode if forced to 0
 * 0: all calls to tablet_set_mode are honored
 */
static int disabled;

int tablet_get_mode(void)
{
	return !!tablet_mode;
}

void tablet_set_mode(int mode)
{
	if (tablet_mode == mode)
		return;

	if (disabled) {
		CPRINTS("Tablet mode set while disabled (ignoring)!");
		return;
	}

	if (hall_sensor_at_360 && !mode) {
		CPRINTS("Ignoring tablet mode exit while hall sensor active.");
		return;
	}

	tablet_mode = mode;
	CPRINTS("tablet mode %sabled", mode ? "en" : "dis");
	hook_notify(HOOK_TABLET_MODE_CHANGE);

#ifdef CONFIG_HOSTCMD_EVENTS
	/*
	 * When tablet mode changes, send an event to ACPI to retrieve
	 * tablet mode value and send an event to the kernel.
	 */
	host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
#endif
}

/* This ifdef can be removed once we clean up past projects which do own init */
#ifdef CONFIG_HALL_SENSOR
#ifndef HALL_SENSOR_GPIO_L
#error  HALL_SENSOR_GPIO_L must be defined
#endif
static void hall_sensor_interrupt_debounce(void)
{
	hall_sensor_at_360 = IS_ENABLED(CONFIG_HALL_SENSOR_CUSTOM)
				     ? board_sensor_at_360()
				     : !gpio_get_level(HALL_SENSOR_GPIO_L);

	/*
	 * 1. Peripherals are disabled only when lid reaches 360 position (It's
	 * probably already disabled by motion_sense task). We deliberately do
	 * not enable peripherals when the lid is leaving 360 position. Instead,
	 * we let motion sense task enable it once it is reaches laptop zone
	 * (180 or less).
	 * 2. Similarly, tablet mode is set here when lid reaches 360
	 * position. It should already be set by motion lid driver. We
	 * deliberately do not clear tablet mode when lid is leaving 360
	 * position(if motion lid driver is used). Instead, we let motion lid
	 * driver to clear it when lid goes into laptop zone.
	 */

	if (!IS_ENABLED(CONFIG_LID_ANGLE) || hall_sensor_at_360)
		tablet_set_mode(hall_sensor_at_360);

	if (IS_ENABLED(CONFIG_LID_ANGLE_UPDATE) && hall_sensor_at_360)
		lid_angle_peripheral_enable(0);
}
DECLARE_DEFERRED(hall_sensor_interrupt_debounce);

/* Debounce time for hall sensor interrupt */
#define HALL_SENSOR_DEBOUNCE_US    (30 * MSEC)

void hall_sensor_isr(enum gpio_signal signal)
{
	hook_call_deferred(&hall_sensor_interrupt_debounce_data,
			   HALL_SENSOR_DEBOUNCE_US);
}

static void hall_sensor_init(void)
{
	/* If this sub-system was disabled before initializing, honor that. */
	if (disabled)
		return;

	gpio_enable_interrupt(HALL_SENSOR_GPIO_L);
	/*
	 * Ensure tablet mode is initialized according to the hardware state
	 * so that the cached state reflects reality.
	 */
	hall_sensor_interrupt_debounce();
}
DECLARE_HOOK(HOOK_INIT, hall_sensor_init, HOOK_PRIO_DEFAULT);

void hall_sensor_disable(void)
{
	gpio_disable_interrupt(HALL_SENSOR_GPIO_L);
	/* Cancel any pending debounce calls */
	hook_call_deferred(&hall_sensor_interrupt_debounce_data, -1);
	tablet_set_mode(0);
	disabled = 1;
}
#endif
