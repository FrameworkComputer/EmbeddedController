/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button state machine for x86 platforms */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTF(format, args...) cprintf(CC_SWITCH, format, ## args)

/*
 * When chipset is on, we stretch the power button signal to it so chipset
 * hard-reset is triggered at ~8 sec, not ~4 sec:
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
/* TODO: link to full power button / lid switch state machine description. */
#define PWRBTN_DELAY_T0    (32 * MSEC)  /* 32ms (PCH requires >16ms) */
#define PWRBTN_DELAY_T1    (4 * SECOND - PWRBTN_DELAY_T0)  /* 4 secs - t0 */
/*
 * Length of time to stretch initial power button press to give chipset a
 * chance to wake up (~100ms) and react to the press (~16ms).  Also used as
 * pulse length for simulated power button presses when the system is off.
 */
#define PWRBTN_INITIAL_US  (200 * MSEC)

enum power_button_state {
	/* Button up; state machine idle */
	PWRBTN_STATE_IDLE = 0,
	/* Button pressed; debouncing done */
	PWRBTN_STATE_PRESSED,
	/* Button down, chipset on; sending initial short pulse */
	PWRBTN_STATE_T0,
	/* Button down, chipset on; delaying until we should reassert signal */
	PWRBTN_STATE_T1,
	/* Button down, signal asserted to chipset */
	PWRBTN_STATE_HELD,
	/* Force pulse due to lid-open event */
	PWRBTN_STATE_LID_OPEN,
	/* Button released; debouncing done */
	PWRBTN_STATE_RELEASED,
	/* Ignore next button release */
	PWRBTN_STATE_EAT_RELEASE,
	/*
	 * Need to power on system after init, but waiting to find out if
	 * sufficient battery power.
	 */
	PWRBTN_STATE_INIT_ON,
	/* Forced pulse at EC boot due to keyboard controlled reset */
	PWRBTN_STATE_BOOT_KB_RESET,
	/* Power button pressed when chipset was off; stretching pulse */
	PWRBTN_STATE_WAS_OFF,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_IDLE;

static const char * const state_names[] = {
	"idle",
	"pressed",
	"t0",
	"t1",
	"held",
	"lid-open",
	"released",
	"eat-release",
	"init-on",
	"recovery",
	"was-off",
};

/*
 * Time for next state transition of power button state machine, or 0 if the
 * state doesn't have a timeout.
 */
static uint64_t tnext_state;

static void set_pwrbtn_to_pch(int high)
{
	/*
	 * If the battery is discharging and low enough we'd shut down the
	 * system, don't press the power button.
	 */
	if (!high && charge_want_shutdown()) {
		CPRINTF("[%T PB PCH pwrbtn ignored due to battery level\n");
		high = 1;
	}

	CPRINTF("[%T PB PCH pwrbtn=%s]\n", high ? "HIGH" : "LOW");
	gpio_set_level(GPIO_PCH_PWRBTN_L, high);
}

/**
 * Handle debounced power button down.
 */
static void power_button_pressed(uint64_t tnow)
{
	CPRINTF("[%T PB pressed]\n");
	pwrbtn_state = PWRBTN_STATE_PRESSED;
	tnext_state = tnow;
}

/**
 * Handle debounced power button up.
 */
static void power_button_released(uint64_t tnow)
{
	CPRINTF("[%T PB released]\n");
	pwrbtn_state = PWRBTN_STATE_RELEASED;
	tnext_state = tnow;
}

/**
 * Set initial power button state.
 */
static void set_initial_pwrbtn_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();

	if (system_jumped_to_this_image() &&
	    chipset_in_state(CHIPSET_STATE_ON)) {
		/*
		 * Jumped to this image while the chipset was already on, so
		 * simply reflect the actual power button state.
		 */
		if (power_button_is_pressed()) {
			CPRINTF("[%T PB init-jumped-held]\n");
			set_pwrbtn_to_pch(0);
		} else {
			CPRINTF("[%T PB init-jumped]\n");
		}
	} else if ((reset_flags & RESET_FLAG_AP_OFF) ||
		   (keyboard_scan_get_boot_key() == BOOT_KEY_DOWN_ARROW)) {
		/*
		 * Reset triggered by keyboard-controlled reset, and down-arrow
		 * was held down.  Or reset flags request AP off.
		 *
		 * Leave the main processor off.  This is a fail-safe
		 * combination for debugging failures booting the main
		 * processor.
		 *
		 * Don't let the PCH see that the power button was pressed.
		 * Otherwise, it might power on.
		 */
		CPRINTF("[%T PB init-off]\n");
		set_pwrbtn_to_pch(1);
		if (power_button_is_pressed())
			pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
		else
			pwrbtn_state = PWRBTN_STATE_IDLE;
	} else {
		/*
		 * All other EC reset conditions power on the main processor so
		 * it can verify the EC.
		 */
		CPRINTF("[%T PB init-on]\n");
		pwrbtn_state = PWRBTN_STATE_INIT_ON;
	}
}

/**
 * Power button state machine.
 *
 * @param tnow		Current time from usec counter
 */
static void state_machine(uint64_t tnow)
{
	/* Not the time to move onto next state */
	if (tnow < tnext_state)
		return;

	/* States last forever unless otherwise specified */
	tnext_state = 0;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_PRESSED:
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/*
			 * Chipset is off, so wake the chipset and send it a
			 * long enough pulse to wake up.  After that we'll
			 * reflect the true power button state.  If we don't
			 * stretch the pulse here, the user may release the
			 * power button before the chipset finishes waking from
			 * hard off state.
			 */
			chipset_exit_hard_off();
			tnext_state = tnow + PWRBTN_INITIAL_US;
			pwrbtn_state = PWRBTN_STATE_WAS_OFF;
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
		/*
		 * If the chipset is already off, don't tell it the power
		 * button is down; it'll just cause the chipset to turn on
		 * again.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			CPRINTF("[%T PB chipset already off]\n");
		else
			set_pwrbtn_to_pch(0);
		pwrbtn_state = PWRBTN_STATE_HELD;
		break;
	case PWRBTN_STATE_RELEASED:
	case PWRBTN_STATE_LID_OPEN:
		set_pwrbtn_to_pch(1);
		pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
	case PWRBTN_STATE_INIT_ON:
		/*
		 * Don't do anything until the charger knows the battery level.
		 * Otherwise we could power on the AP only to shut it right
		 * back down due to insufficient battery.
		 */
#ifdef HAS_TASK_CHARGER
		if (charge_get_state() == PWR_STATE_INIT)
			break;
#endif

		/*
		 * Power the system on if possible.  Gating due to insufficient
		 * battery is handled inside set_pwrbtn_to_pch().
		 */
		chipset_exit_hard_off();
		set_pwrbtn_to_pch(0);
		tnext_state = get_time().val + PWRBTN_INITIAL_US;

		if (power_button_is_pressed()) {
			if (system_get_reset_flags() & RESET_FLAG_RESET_PIN)
				pwrbtn_state = PWRBTN_STATE_BOOT_KB_RESET;
			else
				pwrbtn_state = PWRBTN_STATE_WAS_OFF;
		} else {
			pwrbtn_state = PWRBTN_STATE_RELEASED;
		}

		break;

	case PWRBTN_STATE_BOOT_KB_RESET:
		/* Initial forced pulse is done.  Ignore the actual power
		 * button until it's released, so that holding down the
		 * recovery combination doesn't cause the chipset to shut back
		 * down. */
		set_pwrbtn_to_pch(1);
		if (power_button_is_pressed())
			pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
		else
			pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
	case PWRBTN_STATE_WAS_OFF:
		/* Done stretching initial power button signal, so show the
		 * true power button state to the PCH. */
		if (power_button_is_pressed()) {
			/* User is still holding the power button */
			pwrbtn_state = PWRBTN_STATE_HELD;
		} else {
			/* Stop stretching the power button press */
			power_button_released(tnow);
		}
		break;
	case PWRBTN_STATE_IDLE:
	case PWRBTN_STATE_HELD:
	case PWRBTN_STATE_EAT_RELEASE:
		/* Do nothing */
		break;
	}
}

void power_button_task(void)
{
	uint64_t t;
	uint64_t tsleep;

	while (1) {
		t = get_time().val;

		/* Update state machine */
		CPRINTF("[%T PB task %d = %s]\n", pwrbtn_state,
			state_names[pwrbtn_state]);

		state_machine(t);

		/* Sleep until our next timeout */
		tsleep = -1;
		if (tnext_state && tnext_state < tsleep)
			tsleep = tnext_state;
		t = get_time().val;
		if (tsleep > t) {
			unsigned d = tsleep == -1 ? -1 : (unsigned)(tsleep - t);
			/*
			 * (Yes, the conversion from uint64_t to unsigned could
			 * theoretically overflow if we wanted to sleep for
			 * more than 2^32 us, but our timeouts are small enough
			 * that can't happen - and even if it did, we'd just go
			 * back to sleep after deciding that we woke up too
			 * early.)
			 */
			CPRINTF("[%T PB task %d = %s, wait %d]\n", pwrbtn_state,
			state_names[pwrbtn_state], d);
			task_wait_event(d);
		}
	}
}

/*****************************************************************************/
/* Hooks */

static void powerbtn_x86_init(void)
{
	set_initial_pwrbtn_state();
}
DECLARE_HOOK(HOOK_INIT, powerbtn_x86_init, HOOK_PRIO_DEFAULT);

/**
 * Handle switch changes based on lid event.
 */
static void powerbtn_x86_lid_change(void)
{
	/* If chipset is off, pulse the power button on lid open to wake it. */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		chipset_exit_hard_off();
		set_pwrbtn_to_pch(0);
		pwrbtn_state = PWRBTN_STATE_LID_OPEN;
		tnext_state = get_time().val + PWRBTN_INITIAL_US;
		task_wake(TASK_ID_POWERBTN);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, powerbtn_x86_lid_change, HOOK_PRIO_DEFAULT);

/**
 * Handle debounced power button changing state.
 */
static void powerbtn_x86_changed(void)
{
	if (pwrbtn_state == PWRBTN_STATE_BOOT_KB_RESET ||
	    pwrbtn_state == PWRBTN_STATE_INIT_ON ||
	    pwrbtn_state == PWRBTN_STATE_LID_OPEN ||
	    pwrbtn_state == PWRBTN_STATE_WAS_OFF) {
		/* Ignore all power button changes during an initial pulse */
		CPRINTF("[%T PB ignoring change]\n");
		return;
	}

	if (power_button_is_pressed()) {
		/* Power button pressed */
		power_button_pressed(get_time().val);
	} else {
		/* Power button released */
		if (pwrbtn_state == PWRBTN_STATE_EAT_RELEASE) {
			/*
			 * Ignore the first power button release if we already
			 * told the PCH the power button was released.
			 */
			CPRINTF("[%T PB ignoring release]\n");
			pwrbtn_state = PWRBTN_STATE_IDLE;
			return;
		}

		power_button_released(get_time().val);
	}

	/* Wake the power button task */
	task_wake(TASK_ID_POWERBTN);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_x86_changed, HOOK_PRIO_DEFAULT);

/**
 * Handle charge state changes
 */
static void powerbtn_x86_charge(void)
{
	/*
	 * If we were waiting for the charge state machine to init before we
	 * powered on the chipset, we can stop waiting.
	 */
	if (pwrbtn_state == PWRBTN_STATE_INIT_ON)
		task_wake(TASK_ID_POWERBTN);
}
DECLARE_HOOK(HOOK_CHARGE_STATE_CHANGE, powerbtn_x86_charge, HOOK_PRIO_DEFAULT);
