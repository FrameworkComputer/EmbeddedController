/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "acpi.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "tablet_mode.h"
#include "timer.h"

#include <stdbool.h>
#include <string.h>

#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_LID, format, ##args)

/*
 * Other code modules assume that notebook mode (i.e. tablet_mode = 0) at
 * startup.
 * tablet_mode is mask, one bit for each source that can trigger a change to
 * tablet mode:
 * - TABLET_TRIGGER_LID: the lid angle is over the threshold.
 * - TABLET_TRIGGER_BASE: the detachable keyboard is disconnected.
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

/* True if the tablet GMR sensor is reporting 360 degrees. */
STATIC_IF(CONFIG_GMR_TABLET_MODE) bool gmr_sensor_at_360;

/* True if the lid GMR sensor is reporting 0 degrees. */
STATIC_IF(CONFIG_GMR_TABLET_MODE) bool gmr_sensor_at_0;

/*
 * True: all calls to tablet_set_mode are ignored and tablet_mode if forced to 0
 * False: all calls to tablet_set_mode are honored
 */
static bool disabled;

static const char *const tablet_mode_names[] = {
	"clamshell",
	"tablet",
};
BUILD_ASSERT(ARRAY_SIZE(tablet_mode_names) == 2);

int tablet_get_mode(void)
{
	return !!tablet_mode;
}

static inline void print_tablet_mode(void)
{
	CPRINTS("%s mode", tablet_mode_names[tablet_get_mode()]);
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
	uint32_t new_mode = tablet_mode_forced ? tablet_mode_store :
						 tablet_mode;
	uint32_t old_mode = 0;

	if (disabled) {
		/*
		 * If tablet mode is being forced by the user, then this logging
		 * would be misleading since the mode wouldn't change anyway, so
		 * skip it.
		 */
		if (!tablet_mode_forced)
			CPRINTS("Tablet mode set while disabled (ignoring)!");
		return;
	}

	if (IS_ENABLED(CONFIG_GMR_TABLET_MODE) &&
	    ((gmr_sensor_at_360 && !mode) || (gmr_sensor_at_0 && mode))) {
		/*
		 * If tablet mode is being forced by the user, then this logging
		 * would be misleading since the mode wouldn't change anyway, so
		 * skip it.
		 */
		if (!tablet_mode_forced)
			CPRINTS("Ignoring %s mode entry while gmr sensors "
				"reports lid %s",
				tablet_mode_names[mode],
				(gmr_sensor_at_360 ? "flipped" : "closed"));
		return;
	}

	if (mode)
		new_mode |= trigger;
	else
		new_mode &= ~trigger;

	if (tablet_mode_forced) {
		/*
		 * Save the current mode based on the HW orientation, so we
		 * apply the correct mode if tablet mode no longer forced in the
		 * future. Don't notify of the tablet mode change yet, since
		 * that will be done as part of handling 'tabletmode reset'.
		 */
		tablet_mode_store = new_mode;
		return;
	}

	old_mode = tablet_mode;
	tablet_mode = new_mode;

	/* Boolean comparison */
	if (!tablet_mode == !old_mode)
		return;

	notify_tablet_mode_change();
}

void tablet_disable(void)
{
	bool need_to_notify = false;

	/* Already disabled, nothing to do. */
	if (disabled)
		return;

	disabled = true;
	/*
	 * We may have already transition to tablet mode:
	 * At board init time, while the sensors are not running yet, we may
	 * have read the Tablet GMR GPIO already, and transitioned to tablet
	 * mode, especially if the GMR is not stuffed and the GPIO is floating.
	 */
	if (tablet_get_mode())
		need_to_notify = true;
	tablet_mode = 0;
	if (need_to_notify)
		notify_tablet_mode_change();
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
	 * When tablet mode is only decided by the GMR sensor (or
	 * or substitute, send the tablet_mode change request.
	 */
	if (!IS_ENABLED(CONFIG_LID_ANGLE))
		tablet_set_mode(gmr_sensor_at_360, TABLET_TRIGGER_LID);

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
	 * 3. However, there is a potential race condition with
	 * tablet_mode_lid_event() which can be triggered before this debounce
	 * function is called, with |gmr_sensor_at_360| still true.
	 * When we notice the lid is closed when the this function is called and
	 * |gmr_sensor_at_360| false, send a transition to go in clamshell mode.
	 * It would mean the user was able to transition in less than ~10ms...
	 */
	if (IS_ENABLED(CONFIG_LID_ANGLE)) {
		if (gmr_sensor_at_360)
			tablet_set_mode(1, TABLET_TRIGGER_LID);
		else if (gmr_sensor_at_0)
			tablet_set_mode(0, TABLET_TRIGGER_LID);
	}

	if (IS_ENABLED(CONFIG_LID_ANGLE_UPDATE) && gmr_sensor_at_360)
		lid_angle_peripheral_enable(0);
}
DECLARE_DEFERRED(gmr_tablet_switch_interrupt_debounce);

/*
 * Debounce time for gmr sensor tablet mode interrupt
 * There could be a race between the GMR sensor for the tablet and
 * the GMR sensor for the lid changes state at the same time.
 * We let the lid sensor GMR debouce first. We would be able to go in tablet
 * mode when LID_OPEN goes from low to high and TABLET_MODE_L goes from high
 * to low.
 * However, in the opposite case, the debouce lid angle interrupt will request
 * the tablet_mode to be clamshell, but |gmr_sensor_at_360| will still be true,
 * the request will be ignored.
 */

void gmr_tablet_switch_isr(enum gpio_signal signal)
{
	hook_call_deferred(&gmr_tablet_switch_interrupt_debounce_data,
			   CONFIG_GMR_SENSOR_DEBOUNCE_US);
}

/*
 * tablet gmr sensor() calls tablet_set_mode() to go in tablet mode
 * when we know for sure the tablet is in tablet mode,
 *
 * It would call tablet_set_mode() to get out only when there are not
 * accelerometer, as we want to get out at ~180 degree.
 * But if for some reason the accelerometers are not working, we won't get
 * out of tablet mode. Therefore, we need a similar function to go in
 * clamshell mode when the lid is closed.
 */
static __maybe_unused void tablet_mode_lid_event(void)
{
	if (!lid_is_open()) {
		gmr_sensor_at_0 = true;
		tablet_set_mode(0, TABLET_TRIGGER_LID);
		if (IS_ENABLED(CONFIG_LID_ANGLE_UPDATE))
			lid_angle_peripheral_enable(1);
	} else {
		gmr_sensor_at_0 = false;
	}
}
#if defined(CONFIG_LID_ANGLE) && defined(CONFIG_LID_SWITCH)
DECLARE_HOOK(HOOK_LID_CHANGE, tablet_mode_lid_event, HOOK_PRIO_DEFAULT);
#endif

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
	if (IS_ENABLED(CONFIG_LID_ANGLE) && IS_ENABLED(CONFIG_LID_SWITCH))
		tablet_mode_lid_event();
}
DECLARE_HOOK(HOOK_INIT, gmr_tablet_switch_init, HOOK_PRIO_POST_LID);

void gmr_tablet_switch_disable(void)
{
	gpio_disable_interrupt(GPIO_TABLET_MODE_L);
	/* Cancel any pending debounce calls */
	hook_call_deferred(&gmr_tablet_switch_interrupt_debounce_data, -1);
	tablet_disable();
}
#endif /* CONFIG_GMR_TABLET_MODE */

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

	/* |+1| to also make sure the strings the same length. */
	if (strncmp(argv[1], "on", strlen("on") + 1) == 0) {
		tablet_mode = TABLET_TRIGGER_LID;
		tablet_mode_forced = true;
	} else if (strncmp(argv[1], "off", strlen("off") + 1) == 0) {
		tablet_mode = 0;
		tablet_mode_forced = true;
	} else if (strncmp(argv[1], "reset", strlen("reset") + 1) == 0) {
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

#ifdef CONFIG_PLATFORM_EC_EXTERNAL_NOTEBOOK_MODE
static void notify_ec_for_nb_mode_change(void)
{
	/*
	 * The gpio_nb_mode pin is an output from SOC (ISH) to EC.
	 *
	 * In this config, ISH runs motion sense task; while EC doesn't.
	 * When ISH motion sense task detects notebook(clamshell)/tablet mode
	 * changes, ISH will notify EC about the change by updating this pin.
	 *
	 * Assert this gpio if changing to notebook(clamshell) mode;
	 * Deassert this gpio if changing to tablet mode.
	 */
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_nb_mode), !tablet_get_mode());
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, notify_ec_for_nb_mode_change,
	     HOOK_PRIO_DEFAULT);
#endif
