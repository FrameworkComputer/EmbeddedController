/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "stdbool.h"
#include "tablet_mode.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_LID, format, ##args)

/*
 * Other code modules assume that notebook mode (i.e. tablet_mode = 0) at
 * startup
 */
static uint32_t tablet_mode;

/*
 * Console command can force the value of tablet_mode. If tablet_mode_force is
 * true, the all external set call for tablet_mode are ignored.
 */
static bool tablet_mode_forced;

/*
 * Console command can force the value of tablet_mode. If tablet_mode_force is
 * false, use stored tablet mode value before it was (possibly) overridden.
 */
static uint32_t tablet_mode_store;

/* True if GMR sensor is reporting 360 degrees. */
static bool gmr_sensor_at_360;

/*
 * True: all calls to tablet_set_mode are ignored and tablet_mode if forced to 0
 * False: all calls to tablet_set_mode are honored
 */
static bool disabled;

int tablet_get_mode(void)
{
	return !!tablet_mode;
}

static inline void print_tablet_mode(void)
{
	CPRINTS("tablet mode %sabled", tablet_mode ? "en" : "dis");
}

static void notify_tablet_mode_change(void)
{
	print_tablet_mode();
	hook_notify(HOOK_TABLET_MODE_CHANGE);

	/*
	 * When tablet mode changes, send an event to ACPI to retrieve
	 * tablet mode value and send an event to the kernel.
	 */
	if (IS_ENABLED(CONFIG_HOSTCMD_EVENTS))
		host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
}

void tablet_set_mode(int mode, uint32_t trigger)
{
	uint32_t old_mode = tablet_mode;

	/* If tablet_mode is forced via a console command, ignore set. */
	if (tablet_mode_forced)
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

	if (mode)
		tablet_mode |= trigger;
	else
		tablet_mode &= ~trigger;

	/* Boolean comparison */
	if (!tablet_mode == !old_mode)
		return;

	notify_tablet_mode_change();
}

void tablet_disable(void)
{
	tablet_mode = 0;
	disabled = true;
}

/* This ifdef can be removed once we clean up past projects which do own init */
#ifdef CONFIG_GMR_TABLET_MODE
#ifdef CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR
#error The board has GMR sensor
#endif
static void gmr_tablet_switch_interrupt_debounce(void)
{
	gmr_sensor_at_360 = IS_ENABLED(CONFIG_GMR_TABLET_MODE_CUSTOM) ?
				    board_sensor_at_360() :
				    !gpio_get_level(GPIO_TABLET_MODE_L);

	/*
	 * DPTF table is updated only when the board enters/exits completely
	 * flipped tablet mode. If the board has no GMR sensor, we determine
	 * if the board is in completely-flipped tablet mode by lid angle
	 * calculation and update DPTF table when lid angle > 300 degrees.
	 */
	if (IS_ENABLED(CONFIG_HOSTCMD_X86) && IS_ENABLED(CONFIG_DPTF)) {
		acpi_dptf_set_profile_num(
			gmr_sensor_at_360 ? DPTF_PROFILE_FLIPPED_360_MODE :
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
		tablet_set_mode(gmr_sensor_at_360, TABLET_TRIGGER_LID);

	if (IS_ENABLED(CONFIG_LID_ANGLE_UPDATE) && gmr_sensor_at_360)
		lid_angle_peripheral_enable(0);
}
DECLARE_DEFERRED(gmr_tablet_switch_interrupt_debounce);

/* Debounce time for gmr sensor tablet mode interrupt */
#define GMR_SENSOR_DEBOUNCE_US (30 * MSEC)

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

	gpio_enable_interrupt(GPIO_TABLET_MODE_L);
	/*
	 * Ensure tablet mode is initialized according to the hardware state
	 * so that the cached state reflects reality.
	 */
	gmr_tablet_switch_interrupt_debounce();
}
DECLARE_HOOK(HOOK_INIT, gmr_tablet_switch_init, HOOK_PRIO_DEFAULT);

void gmr_tablet_switch_disable(void)
{
	gpio_disable_interrupt(GPIO_TABLET_MODE_L);
	/* Cancel any pending debounce calls */
	hook_call_deferred(&gmr_tablet_switch_interrupt_debounce_data, -1);
	tablet_disable();
}
#endif

static enum ec_status tablet_mode_command(struct host_cmd_handler_args *args)
{
	const struct ec_params_set_tablet_mode *p = args->params;

	if (tablet_mode_forced == false)
		tablet_mode_store = tablet_mode;

	switch (p->tablet_mode) {
	case TABLET_MODE_DEFAULT:
		tablet_mode = tablet_mode_store;
		tablet_mode_forced = false;
		break;
	case TABLET_MODE_FORCE_TABLET:
		tablet_mode = TABLET_TRIGGER_LID;
		tablet_mode_forced = true;
		break;
	case TABLET_MODE_FORCE_CLAMSHELL:
		tablet_mode = 0;
		tablet_mode_forced = true;
		break;
	default:
		CPRINTS("Invalid EC_CMD_SET_TABLET_MODE parameter: %d",
			p->tablet_mode);
		return EC_RES_INVALID_PARAM;
	}

	notify_tablet_mode_change();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_TABLET_MODE, tablet_mode_command,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

#ifdef CONFIG_TABLET_MODE
static int command_settabletmode(int argc, const char **argv)
{
	if (argc == 1) {
		print_tablet_mode();
		return EC_SUCCESS;
	}

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	if (tablet_mode_forced == false)
		tablet_mode_store = tablet_mode;

	if (argv[1][0] == 'o' && argv[1][1] == 'n') {
		tablet_mode = TABLET_TRIGGER_LID;
		tablet_mode_forced = true;
	} else if (argv[1][0] == 'o' && argv[1][1] == 'f') {
		tablet_mode = 0;
		tablet_mode_forced = true;
	} else if (argv[1][0] == 'r') {
		tablet_mode = tablet_mode_store;
		tablet_mode_forced = false;
	} else {
		return EC_ERROR_PARAM1;
	}

	notify_tablet_mode_change();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tabletmode, command_settabletmode, "[on | off | reset]",
			"Manually force tablet mode to on, off or reset.");
#endif

__test_only void tablet_reset(void)
{
	tablet_mode = 0;
	tablet_mode_forced = false;
	disabled = false;
}
