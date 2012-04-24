/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button and lid switch module for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "eoption.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "power_button.h"
#include "pwm.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_POWERBTN, outstr)
#define CPRINTF(format, args...) cprintf(CC_POWERBTN, format, ## args)

/* Power button state machine.
 *
 *   PWRBTN#   ---                      ----
 *     to EC     |______________________|
 *
 *
 *   PWRBTN#   ---  ---------           ----
 *    to PCH     |__|       |___________|
 *                t0    t1    held down
 *
 *   scan code   |                      |
 *    to host    v                      v
 *     @S0   make code             break code
 */
#define PWRBTN_DEBOUNCE_US 30000  /* Debounce time for power button */
#define PWRBTN_DELAY_T0    32000  /* 32ms (PCH requires >16ms) */
#define PWRBTN_DELAY_T1    (4000000 - PWRBTN_DELAY_T0)  /* 4 secs - t0 */
#define PWRBTN_RECOVERY_US 200000 /* Length of time to simulate power button
				   * press when booting into
				   * keyboard-controlled recovery mode */

#define LID_DEBOUNCE_US    30000  /* Debounce time for lid switch */
#define LID_PWRBTN_US      PWRBTN_DELAY_T0 /* Length of time to simulate power
					    * button press on lid open */

enum power_button_state {
	PWRBTN_STATE_STOPPED = 0,
	PWRBTN_STATE_START,
	PWRBTN_STATE_T0,
	PWRBTN_STATE_T1,
	PWRBTN_STATE_HELD_DOWN,
	PWRBTN_STATE_STOPPING,
	PWRBTN_STATE_BOOT_RESET,
	PWRBTN_STATE_BOOT_RECOVERY,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_STOPPED;

/* Time for next state transition of power button state machine, or 0 if the
 * state doesn't have a timeout. */
static uint64_t tnext_state;

/* Debounce timeouts for power button and lid switch.  0 means the signal is
 * stable (not being debounced). */
static uint64_t tdebounce_lid;
static uint64_t tdebounce_pwr;

static uint8_t *memmap_switches;
static int debounced_lid_open;

/* Update status of non-debounced switches */
static void update_other_switches(void)
{
	if (gpio_get_level(GPIO_WRITE_PROTECT) == 0)
		*memmap_switches |= EC_LPC_SWITCH_WRITE_PROTECT_DISABLED;
	else
		*memmap_switches &= ~EC_LPC_SWITCH_WRITE_PROTECT_DISABLED;

	if (keyboard_scan_recovery_pressed())
		*memmap_switches |= EC_LPC_SWITCH_KEYBOARD_RECOVERY;
	else
		*memmap_switches &= ~EC_LPC_SWITCH_KEYBOARD_RECOVERY;

	if (gpio_get_level(GPIO_RECOVERYn) == 0)
		*memmap_switches |= EC_LPC_SWITCH_DEDICATED_RECOVERY;
	else
		*memmap_switches &= ~EC_LPC_SWITCH_DEDICATED_RECOVERY;

#ifdef CONFIG_FAKE_DEV_SWITCH
	if (eoption_get_bool(EOPTION_BOOL_FAKE_DEV))
		*memmap_switches |= EC_LPC_SWITCH_FAKE_DEVELOPER;
	else
		*memmap_switches &= ~EC_LPC_SWITCH_FAKE_DEVELOPER;
#endif
}


static void set_pwrbtn_to_pch(int high)
{
	CPRINTF("[%T PB PCH pwrbtn=%s]\n", high ? "HIGH" : "LOW");
	gpio_set_level(GPIO_PCH_PWRBTNn, high);
}


/* Return 1 if power button is pressed, 0 if not pressed. */
static int get_power_button_pressed(void)
{
	return gpio_get_level(GPIO_POWER_BUTTONn) ? 0 : 1;
}


static void update_backlight(void)
{
	/* Only enable the backlight if the lid is open */
	if (gpio_get_level(GPIO_PCH_BKLTEN) && debounced_lid_open)
		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
	else
		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);

	/* Same with keyboard backlight */
	pwm_enable_keyboard_backlight(debounced_lid_open);
}


/* Power button state machine.  Passed current time from usec counter. */
static void state_machine(uint64_t tnow)
{
	/* Not the time to move onto next state */
	if (tnow < tnext_state)
		return;

	/* States last forever unless otherwise specified */
	tnext_state = 0;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_START:
		if (chipset_in_state(CHIPSET_STATE_SOFT_OFF)) {
			/* Chipset is off, so just pass the true power button
			 * state to the chipset. */
			pwrbtn_state = PWRBTN_STATE_HELD_DOWN;
		} else {
			/* Chipset is on, so send the chipset a pulse */
			tnext_state = tnow + PWRBTN_DELAY_T0;
			pwrbtn_state = PWRBTN_STATE_T0;
		}
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_T0:
		tnext_state = tnow + PWRBTN_DELAY_T1;
		pwrbtn_state = PWRBTN_STATE_T1;
		set_pwrbtn_to_pch(1);
		break;
	case PWRBTN_STATE_T1:
		/* If the chipset is already off, don't tell it the power
		 * button is down; it'll just cause the chipset to turn on
		 * again. */
		if (!chipset_in_state(CHIPSET_STATE_SOFT_OFF))
			set_pwrbtn_to_pch(0);
		else
			CPRINTF("[%T PB chipset already off]\n");
		pwrbtn_state = PWRBTN_STATE_HELD_DOWN;
		break;
	case PWRBTN_STATE_STOPPING:
		set_pwrbtn_to_pch(1);
		pwrbtn_state = PWRBTN_STATE_STOPPED;
		break;
	case PWRBTN_STATE_BOOT_RECOVERY:
		set_pwrbtn_to_pch(1);
		pwrbtn_state = PWRBTN_STATE_BOOT_RESET;
		break;
	case PWRBTN_STATE_STOPPED:
	case PWRBTN_STATE_HELD_DOWN:
	case PWRBTN_STATE_BOOT_RESET:
		/* Do nothing */
		break;
	}
}


/* Handle debounced power button changing state */
static void power_button_changed(uint64_t tnow)
{
	if (pwrbtn_state == PWRBTN_STATE_BOOT_RECOVERY) {
		/* Ignore all power button changes during the recovery pulse */
		CPRINTF("[%T PB changed during recovery pulse]\n");
	} else if (get_power_button_pressed()) {
		/* Power button pressed */
		CPRINTF("[%T PB pressed]\n");
		pwrbtn_state = PWRBTN_STATE_START;
		tnext_state = tnow;
		*memmap_switches |= EC_LPC_SWITCH_POWER_BUTTON_PRESSED;
		keyboard_set_power_button(1);
		lpc_set_host_events(
			EC_LPC_HOST_EVENT_MASK(EC_LPC_HOST_EVENT_POWER_BUTTON));
	} else if (pwrbtn_state == PWRBTN_STATE_BOOT_RESET) {
		/* Ignore the first power button release after a
		 * keyboard-controlled reset, since we already told the PCH the
		 * power button was released. */
		CPRINTF("[%T PB released after keyboard reset]\n");
		pwrbtn_state = PWRBTN_STATE_STOPPED;
	} else {
		/* Power button released normally (outside of a
		 * keyboard-controlled reset) */
		CPRINTF("[%T PB released]\n");
		pwrbtn_state = PWRBTN_STATE_STOPPING;
		tnext_state = tnow;
		*memmap_switches &= ~EC_LPC_SWITCH_POWER_BUTTON_PRESSED;
		keyboard_set_power_button(0);
	}
}


/* Lid open */
static void lid_switch_open(uint64_t tnow)
{
	CPRINTF("[%T PB lid open]\n");

	debounced_lid_open = 1;
	*memmap_switches |= EC_LPC_SWITCH_LID_OPEN;

	lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(
			    EC_LPC_HOST_EVENT_LID_OPEN));

	/* If the chipset is is soft-off, send a power button pulse to
	 * wake up the chipset. */
	if (chipset_in_state(CHIPSET_STATE_SOFT_OFF)) {
		set_pwrbtn_to_pch(0);
		pwrbtn_state = PWRBTN_STATE_STOPPING;
		tnext_state = tnow + LID_PWRBTN_US;
		task_wake(TASK_ID_POWERBTN);
	}
}


/* Lid close */
static void lid_switch_close(uint64_t tnow)
{
	CPRINTF("[%T PB lid close]\n");

	debounced_lid_open = 0;
	*memmap_switches &= ~EC_LPC_SWITCH_LID_OPEN;

	lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(
			    EC_LPC_HOST_EVENT_LID_CLOSED));
}


/* Handle debounced lid switch changing state */
static void lid_switch_changed(uint64_t tnow)
{
	if (gpio_get_level(GPIO_LID_SWITCHn))
		lid_switch_open(tnow);
	else
		lid_switch_close(tnow);

	update_backlight();
}


void power_button_interrupt(enum gpio_signal signal)
{
	/* Reset debounce time for the changed signal */
	switch (signal) {
	case GPIO_LID_SWITCHn:
		tdebounce_lid = get_time().val + LID_DEBOUNCE_US;
		break;
	case GPIO_POWER_BUTTONn:
		tdebounce_pwr = get_time().val + PWRBTN_DEBOUNCE_US;
		break;
	case GPIO_PCH_BKLTEN:
		update_backlight();
		break;
	default:
		/* Non-debounced switches; we'll update their state
		 * automatically the next time through the task loop. */
		break;
	}

	/* We don't have a way to tell the task to wake up at the end of the
         * debounce interval; wake it up now so it can go back to sleep for the
         * remainder of the interval.  The alternative would be to have the
         * task wake up _every_ debounce_us on its own; that's less desirable
         * when the EC should be sleeping. */
	task_wake(TASK_ID_POWERBTN);
}


static int power_button_init(void)
{
	/* Set up memory-mapped switch positions */
	memmap_switches = lpc_get_memmap_range() + EC_LPC_MEMMAP_SWITCHES;
	*memmap_switches = 0;
	if (gpio_get_level(GPIO_LID_SWITCHn) != 0) {
		debounced_lid_open = 1;
		*memmap_switches |= EC_LPC_SWITCH_LID_OPEN;
	}
	update_other_switches();
	update_backlight();

	if (system_get_reset_cause() == SYSTEM_RESET_RESET_PIN) {
		/* Reset triggered by keyboard-controlled reset, so override
		 * the power button signal to the PCH. */
		if (keyboard_scan_recovery_pressed()) {
			/* In recovery mode, so send a power button pulse to
			 * the PCH so it powers on. */
			set_pwrbtn_to_pch(0);
			pwrbtn_state = PWRBTN_STATE_BOOT_RECOVERY;
			tnext_state = get_time().val + PWRBTN_RECOVERY_US;
		} else {
			/* Keyboard-controlled reset, so don't let the PCH see
			 * that the power button was pressed.  Otherwise, it
			 * might power on. */
			set_pwrbtn_to_pch(1);
			pwrbtn_state = PWRBTN_STATE_BOOT_RESET;
		}
	} else {
		/* Copy initial power button state to PCH and memory-mapped
		 * switch positions. */
		set_pwrbtn_to_pch(get_power_button_pressed() ? 0 : 1);
		if (get_power_button_pressed())
			*memmap_switches |= EC_LPC_SWITCH_POWER_BUTTON_PRESSED;
	}

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_POWER_BUTTONn);
	gpio_enable_interrupt(GPIO_LID_SWITCHn);
	gpio_enable_interrupt(GPIO_WRITE_PROTECT);
	gpio_enable_interrupt(GPIO_RECOVERYn);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_DEFAULT);


void power_button_task(void)
{
	uint64_t t;
	uint64_t tsleep;
	while (1) {
		t = get_time().val;

		/* Handle debounce timeouts for power button and lid switch */
		if (tdebounce_pwr && t >= tdebounce_pwr) {
			tdebounce_pwr = 0;
			power_button_changed(t);
		}
		if (tdebounce_lid && t >= tdebounce_lid) {
			tdebounce_lid = 0;
			lid_switch_changed(t);
		}

		/* Handle non-debounced switches */
		update_other_switches();

		/* Update state machine */
		state_machine(t);

		/* Sleep until our next timeout */
		tsleep = -1;
		if (tdebounce_pwr && tdebounce_pwr < tsleep)
			tsleep = tdebounce_pwr;
		if (tdebounce_lid && tdebounce_lid < tsleep)
			tsleep = tdebounce_lid;
		if (tnext_state && tnext_state < tsleep)
			tsleep = tnext_state;
		t = get_time().val;
		if (tsleep > t) {
			unsigned d = tsleep == -1 ? -1 : (unsigned)(tsleep - t);
			/* (Yes, the conversion from uint64_t to unsigned could
			 * theoretically overflow if we wanted to sleep for
			 * more than 2^32 us, but our timeouts are small enough
			 * that can't happen - and even if it did, we'd just go
			 * back to sleep after deciding that we woke up too
			 * early.) */
			CPRINTF("[%T PB task %d wait %d]\n", pwrbtn_state, d);
			task_wait_event(d);
		}
	}
}

/*****************************************************************************/
/* Console commands */

static int command_powerbtn(int argc, char **argv)
{
	int ms = 100;  /* Press duration in ms */
	char *e;

	if (argc > 1) {
		ms = strtoi(argv[1], &e, 0);
		if (*e) {
			ccputs("Invalid duration.\n"
			       "Usage: powerbtn [duration_ms]\n");
			return EC_ERROR_INVAL;
		}
	}

	/* Note that this only simulates the raw power button signal to the
	 * PCH.  It does not simulate the full state machine which sends SMIs
	 * and other events to other parts of the EC and chipset. */
	ccprintf("Simulating %d ms power button press.\n", ms);
	set_pwrbtn_to_pch(0);
	usleep(ms * 1000);
	set_pwrbtn_to_pch(1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn);


static int command_lidopen(int argc, char **argv)
{
	lid_switch_open(get_time().val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidopen, command_lidopen);


static int command_lidclose(int argc, char **argv)
{
	lid_switch_close(get_time().val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidclose, command_lidclose);
