/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
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
static int forced_tablet_mode = -1;

/* 1: GMR sensor is reporting 360 degrees. */
static int gmr_sensor_at_360;

/*
 * 1: all calls to tablet_set_mode are ignored and tablet_mode if forced to 0
 * 0: all calls to tablet_set_mode are honored
 */
static int disabled;

int tablet_get_mode(void)
{
	if (forced_tablet_mode != -1)
		return !!forced_tablet_mode;
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

	if (gmr_sensor_at_360 && !mode) {
		CPRINTS("Ignoring tablet mode exit while gmr sensor "
			"reports 360-degree tablet mode.");
		return;
	}

	tablet_mode = mode;

	if (forced_tablet_mode != -1)
		return;

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

static void tabletmode_force_state(int mode)
{
	if (forced_tablet_mode == mode)
		return;

	forced_tablet_mode = mode;

	hook_notify(HOOK_TABLET_MODE_CHANGE);
	if (IS_ENABLED(CONFIG_HOSTCMD_EVENTS))
		host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
}

void tablet_disable(void)
{
	tablet_mode = 0;
	disabled = 1;
}

/* This ifdef can be removed once we clean up past projects which do own init */
#ifdef CONFIG_GMR_TABLET_MODE
#ifndef GMR_TABLET_MODE_GPIO_L
#error  GMR_TABLET_MODE_GPIO_L must be defined
#endif
#ifdef CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR
#error The board has GMR sensor
#endif
static void gmr_tablet_switch_interrupt_debounce(void)
{
	gmr_sensor_at_360 = IS_ENABLED(CONFIG_GMR_TABLET_MODE_CUSTOM)
				     ? board_sensor_at_360()
				     : !gpio_get_level(GMR_TABLET_MODE_GPIO_L);

	/*
	 * DPTF table is updated only when the board enters/exits completely
	 * flipped tablet mode. If the board has no GMR sensor, we determine
	 * if the board is in completely-flipped tablet mode by lid angle
	 * calculation and update DPTF table when lid angle > 300 degrees.
	 */
	if (IS_ENABLED(CONFIG_HOSTCMD_X86) && IS_ENABLED(CONFIG_DPTF)) {
		acpi_dptf_set_profile_num(gmr_sensor_at_360 ?
					  DPTF_PROFILE_FLIPPED_360_MODE :
					  DPTF_PROFILE_CLAMSHELL);
	}
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

	if (!IS_ENABLED(CONFIG_LID_ANGLE) || gmr_sensor_at_360)
		tablet_set_mode(gmr_sensor_at_360);

	if (IS_ENABLED(CONFIG_LID_ANGLE_UPDATE) && gmr_sensor_at_360)
		lid_angle_peripheral_enable(0);
}
DECLARE_DEFERRED(gmr_tablet_switch_interrupt_debounce);

/* Debounce time for gmr sensor tablet mode interrupt */
#define GMR_SENSOR_DEBOUNCE_US    (30 * MSEC)

void gmr_tablet_switch_isr(enum gpio_signal signal)
{
	hook_call_deferred(&gmr_tablet_switch_interrupt_debounce_data,
			   GMR_SENSOR_DEBOUNCE_US);
}

static void gmr_tablet_switch_init(void)
{
	/* If this sub-system was disabled before initializing, honor that. */
	if (disabled)
		return;

	gpio_enable_interrupt(GMR_TABLET_MODE_GPIO_L);
	/*
	 * Ensure tablet mode is initialized according to the hardware state
	 * so that the cached state reflects reality.
	 */
	gmr_tablet_switch_interrupt_debounce();
}
DECLARE_HOOK(HOOK_INIT, gmr_tablet_switch_init, HOOK_PRIO_DEFAULT);

void gmr_tablet_switch_disable(void)
{
	gpio_disable_interrupt(GMR_TABLET_MODE_GPIO_L);
	/* Cancel any pending debounce calls */
	hook_call_deferred(&gmr_tablet_switch_interrupt_debounce_data, -1);
	tablet_disable();
}
#endif

static int command_settabletmode(int argc, char **argv)
{
	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;
	if (argv[1][0] == 'o' && argv[1][1] == 'n')
		tabletmode_force_state(1);
	else if (argv[1][0] == 'o' && argv[1][1] == 'f')
		tabletmode_force_state(0);
	else if (argv[1][0] == 'r')
		tabletmode_force_state(-1);
	else
		return EC_ERROR_PARAM1;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tabletmode, command_settabletmode,
	"[on | off | reset]",
	"Manually force tablet mode to on, off or reset.");
