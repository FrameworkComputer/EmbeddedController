/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid switch module for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "common.h"
#include "console.h"
#include "gpio.h"
#line 18
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "tablet_mode.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ##args)

/* if no X-macro is defined for LID switch GPIO, use GPIO_LID_OPEN as default */
#ifndef CONFIG_LID_SWITCH_GPIO_LIST
#define CONFIG_LID_SWITCH_GPIO_LIST LID_GPIO(GPIO_LID_OPEN)
#endif

static int debounced_lid_open; /* Debounced lid state */
static int forced_lid_open; /* Forced lid open */

/**
 * Get raw lid switch state.
 *
 * @return 1 if lid is open, 0 if closed.
 */
static int raw_lid_open(void)
{
#define LID_GPIO(gpio) || gpio_get_level(gpio)
	return (forced_lid_open CONFIG_LID_SWITCH_GPIO_LIST) ? 1 : 0;
#undef LID_GPIO
}

/**
 * Handle lid open.
 */
static void lid_switch_open(void)
{
	if (debounced_lid_open) {
		CPRINTS("lid already open");
		return;
	}

	CPRINTS("lid open");
	debounced_lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
#ifdef CONFIG_HOSTCMD_EVENTS
	host_set_single_event(EC_HOST_EVENT_LID_OPEN);
#endif
}

/**
 * Handle lid close.
 */
static void lid_switch_close(void)
{
	if (!debounced_lid_open) {
		CPRINTS("lid already closed");
		return;
	}

#ifdef CONFIG_TABLET_MODE_SKIP_LID_CLOSE
	if (tablet_get_mode()) {
		/* Ignore spurious event */
		CPRINTS("in tablet mode skip lid close");
		return;
	}
#endif
	/* Notify host */
	CPRINTS("lid close");
	debounced_lid_open = 0;
	hook_notify(HOOK_LID_CHANGE);
#ifdef CONFIG_HOSTCMD_EVENTS
	host_set_single_event(EC_HOST_EVENT_LID_CLOSED);
#endif
}

test_mockable int lid_is_open(void)
{
	return debounced_lid_open;
}

/**
 * Lid switch initialization code
 */
static void lid_init(void)
{
	if (raw_lid_open())
		debounced_lid_open = 1;

		/* Enable interrupts, now that we've initialized */
#define LID_GPIO(gpio) gpio_enable_interrupt(gpio);
	CONFIG_LID_SWITCH_GPIO_LIST
#undef LID_GPIO
}
DECLARE_HOOK(HOOK_INIT, lid_init, HOOK_PRIO_INIT_LID);

/**
 * Handle debounced lid switch changing state.
 */
static void lid_change_deferred(void)
{
	const int new_open = raw_lid_open();

	/* If lid hasn't changed state, nothing to do */
	if (new_open == debounced_lid_open)
		return;

	if (new_open)
		lid_switch_open();
	else
		lid_switch_close();
}
DECLARE_DEFERRED(lid_change_deferred);

void lid_interrupt(enum gpio_signal signal)
{
	/* Reset lid debounce time */
	hook_call_deferred(&lid_change_deferred_data, LID_DEBOUNCE_US);
}

void enable_lid_detect(bool enable)
{
	CPRINTS("lid detect %sabled", enable ? "en" : "dis");
	if (enable) {
#define LID_GPIO(gpio) gpio_enable_interrupt(gpio);
		CONFIG_LID_SWITCH_GPIO_LIST
#undef LID_GPIO
	} else {
#define LID_GPIO(gpio) gpio_disable_interrupt(gpio);
		CONFIG_LID_SWITCH_GPIO_LIST
#undef LID_GPIO
		lid_switch_open();
	}
}

static int command_lidopen(int argc, const char **argv)
{
	lid_switch_open();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidopen, command_lidopen, NULL, "Simulate lid open");

static int command_lidclose(int argc, const char **argv)
{
	lid_switch_close();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidclose, command_lidclose, NULL, "Simulate lid close");

static int command_lidstate(int argc, const char **argv)
{
	ccprintf("lid state: %s\n", debounced_lid_open ? "open" : "closed");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidstate, command_lidstate, NULL, "Get state of lid");

/**
 * Host command to enable/disable lid opened.
 */
static enum ec_status hc_force_lid_open(struct host_cmd_handler_args *args)
{
	const struct ec_params_force_lid_open *p = args->params;
	int old_state = forced_lid_open;

	/* Override lid open if necessary */
	forced_lid_open = p->enabled ? 1 : 0;

	/* Make this take effect immediately; no debounce time */
	if (forced_lid_open != old_state)
		hook_call_deferred(&lid_change_deferred_data, 0);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FORCE_LID_OPEN, hc_force_lid_open, EC_VER_MASK(0));

#if defined(HAS_TASK_KEYSCAN) || defined(CONFIG_CROS_EC_KEYBOARD_INPUT)

static void keyboard_lid_change(void)
{
	if (lid_is_open()) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_CLOSED);
	} else {
		keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_CLOSED);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, keyboard_lid_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, keyboard_lid_change, HOOK_PRIO_POST_LID);

#endif
