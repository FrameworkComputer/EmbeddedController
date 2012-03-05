/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button and lid switch module for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "keyboard.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

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


static void set_pwrbtn_to_pch(int high)
{
	uart_printf("[PB PCH pwrbtn=%s]\n", high ? "HIGH" : "LOW");
	gpio_set_level(GPIO_PCH_PWRBTNn, high);
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
		tnext_state = tnow + PWRBTN_DELAY_T0;
		pwrbtn_state = PWRBTN_STATE_T0;
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_T0:
		tnext_state = tnow + PWRBTN_DELAY_T1;
		pwrbtn_state = PWRBTN_STATE_T1;
		set_pwrbtn_to_pch(1);
		break;
	case PWRBTN_STATE_T1:
		pwrbtn_state = PWRBTN_STATE_HELD_DOWN;
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_STOPPING:
		set_pwrbtn_to_pch(1);
		pwrbtn_state = PWRBTN_STATE_STOPPED;
		break;
	case PWRBTN_STATE_STOPPED:
	case PWRBTN_STATE_HELD_DOWN:
		/* Do nothing */
		break;
	}
}


/* Handle debounced power button changing state */
static void power_button_changed(uint64_t tnow)
{
	if (!gpio_get_level(GPIO_POWER_BUTTONn)) {
		/* pressed */
		pwrbtn_state = PWRBTN_STATE_START;
		*memmap_switches |= EC_LPC_SWITCH_POWER_BUTTON_PRESSED;
		keyboard_set_power_button(1);
		lpc_set_host_events(
			EC_LPC_HOST_EVENT_MASK(EC_LPC_HOST_EVENT_POWER_BUTTON));
	} else {
		/* released */
		pwrbtn_state = PWRBTN_STATE_STOPPING;
		*memmap_switches &= ~EC_LPC_SWITCH_POWER_BUTTON_PRESSED;
		keyboard_set_power_button(0);
	}
	tnext_state = tnow;  /* Trigger next state transition now */
}


/* Handle debounced lid switch changing state */
static void lid_switch_changed(uint64_t tnow)
{
	int v = gpio_get_level(GPIO_LID_SWITCHn);
	uart_printf("[PB lid %s]\n", v ? "open" : "closed");

	/* Pass signal on to PCH; this is how the BIOS/OS knows to suspend or
	 * shutdown when the lid is closed. */
	gpio_set_level(GPIO_PCH_LID_SWITCHn, v);

	lpc_set_host_events(EC_LPC_HOST_EVENT_MASK((v ?
		EC_LPC_HOST_EVENT_LID_OPEN : EC_LPC_HOST_EVENT_LID_CLOSED)));

	if (v) {
		/* Lid open */
		*memmap_switches |= EC_LPC_SWITCH_LID_OPEN;

		/* If the chipset is is soft-off, send a power button pulse to
		 * wake up the chipset. */
		if (chipset_in_state(CHIPSET_STATE_SOFT_OFF)) {
			set_pwrbtn_to_pch(0);
			pwrbtn_state = PWRBTN_STATE_STOPPING;
			tnext_state = tnow + LID_PWRBTN_US;
		}
	} else {
		/* Lid closed */
		*memmap_switches &= ~EC_LPC_SWITCH_LID_OPEN;
	}
}


void power_button_interrupt(enum gpio_signal signal)
{
	/* Reset debounce time for the changed signal */
	if (signal == GPIO_LID_SWITCHn)
		tdebounce_lid = get_time().val + LID_DEBOUNCE_US;
	else
		tdebounce_pwr = get_time().val + PWRBTN_DEBOUNCE_US;

	/* We don't have a way to tell the task to wake up at the end of the
         * debounce interval; wake it up now so it can go back to sleep for the
         * remainder of the interval.  The alternative would be to have the
         * task wake up _every_ debounce_us on its own; that's less desirable
         * when the EC should be sleeping. */
	task_send_msg(TASK_ID_POWERBTN, TASK_ID_POWERBTN, 0);
}


int power_button_init(void)
{
	/* Set up memory-mapped switch positions */
	memmap_switches = lpc_get_memmap_range() + EC_LPC_MEMMAP_SWITCHES;
	*memmap_switches = 0;
	if (gpio_get_level(GPIO_POWER_BUTTONn) == 0)
		*memmap_switches |= EC_LPC_SWITCH_POWER_BUTTON_PRESSED;
	if (gpio_get_level(GPIO_PCH_LID_SWITCHn) != 0)
		*memmap_switches |= EC_LPC_SWITCH_LID_OPEN;

	/* Copy initial switch states to PCH */
	gpio_set_level(GPIO_PCH_PWRBTNn, gpio_get_level(GPIO_POWER_BUTTONn));
	gpio_set_level(GPIO_PCH_LID_SWITCHn, gpio_get_level(GPIO_LID_SWITCHn));

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_POWER_BUTTONn);
	gpio_enable_interrupt(GPIO_LID_SWITCHn);

	return EC_SUCCESS;
}


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
			uart_printf("[PB task wait %d]\n", d);
			task_wait_msg(d);
		}
	}
}

/*****************************************************************************/
/* Console commnands */

static int command_powerbtn(int argc, char **argv)
{
	int ms = 100;  /* Press duration in ms */
	char *e;

	if (argc > 1) {
		ms = strtoi(argv[1], &e, 0);
		if (*e) {
			uart_puts("Invalid duration.\n"
				  "Usage: powerbtn [duration_ms]\n");
			return EC_ERROR_INVAL;
		}
	}

	/* Note that this only simulates the raw power button signal to the
	 * PCH.  It does not simulate the full state machine which sends SMIs
	 * and other events to other parts of the EC and chipset. */
	uart_printf("Simulating %d ms power button press.\n", ms);
	set_pwrbtn_to_pch(0);
	usleep(ms * 1000);
	set_pwrbtn_to_pch(1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn);
