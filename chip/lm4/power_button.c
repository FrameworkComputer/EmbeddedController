/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button and lid switch module for Chrome EC */

#include "console.h"
#include "gpio.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

enum debounce_isr_id {
	DEBOUNCE_LID,
	DEBOUNCE_PWRBTN,
	DEBOUNCE_ISR_ID_MAX
};

struct debounce_isr_t {
	/* TODO: Add a carry bit to indicate timestamp overflow */
	timestamp_t tstamp;
	int started;
	void (*callback)(void);
};

struct debounce_isr_t debounce_isr[DEBOUNCE_ISR_ID_MAX];

enum power_button_state {
	PWRBTN_STATE_STOPPED = 0,
	PWRBTN_STATE_START,
	PWRBTN_STATE_T0,
	PWRBTN_STATE_T1,
	PWRBTN_STATE_HELD_DOWN,
	PWRBTN_STATE_STOPPING,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_STOPPED;
/* The next timestamp to move onto next state if power button is still pressed.
 */
static timestamp_t pwrbtn_next_ts = {0};

#define PWRBTN_DELAY_T0 32000  /* 32ms */
#define PWRBTN_DELAY_T1 (4000000 - PWRBTN_DELAY_T0)  /* 4 secs - t0 */


static void lid_switch_isr(void)
{
	/* TODO: Currently we pass through the LID_SW# pin to R_EC_LID_OUT#
	 * directly. Modify this if we need to consider more conditions. */
	gpio_set_level(GPIO_PCH_LID_SWITCHn,
		       gpio_get_level(GPIO_LID_SWITCHn));
}


/* Power button state machine.
 *
 *   PWRBTN#   ---                      ----
 *     to EC     |______________________|
 *
 *
 *   PWRBTN#   ---  ---------           ----
 *    to PCH     |__|       |___________|
 *                t0    t1    held down
 */
static void set_pwrbtn_to_pch(int high)
{
	uart_printf("[%d] set_pwrbtn_to_pch(%s)\n",
		    get_time().le.lo, high ? "HIGH" : "LOW");
	gpio_set_level(GPIO_PCH_PWRBTNn, high);
}


static void pwrbtn_sm_start(void)
{
	pwrbtn_state = PWRBTN_STATE_START;
	pwrbtn_next_ts = get_time();  /* execute action now! */
}


static void pwrbtn_sm_stop(void)
{
	pwrbtn_state = PWRBTN_STATE_STOPPING;
	pwrbtn_next_ts = get_time();  /* execute action now ! */
}


static void pwrbtn_sm_handle(timestamp_t current)
{
	/* Not the time to move onto next state */
	if (current.val < pwrbtn_next_ts.val)
		return;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_START:
		pwrbtn_next_ts.val = current.val + PWRBTN_DELAY_T0;
		pwrbtn_state = PWRBTN_STATE_T0;
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_T0:
		pwrbtn_next_ts.val = current.val + PWRBTN_DELAY_T1;
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


static void power_button_isr(void)
{
	if (!gpio_get_level(GPIO_POWER_BUTTONn)) {
		/* pressed */
		pwrbtn_sm_start();
		/* TODO: implement after chip/lm4/x86_power.c is completed. */
		/* if system is in S5, power_on_system()
		 * elif system is in S3, resume_system()
		 * else S0 i8042_send_host(make_code); */
	} else {
		/* released */
		pwrbtn_sm_stop();
		/* TODO: implement after chip/lm4/x86_power.c is completed. */
		/* if system in S0, i8042_send_host(break_code); */
	}
}


void power_button_interrupt(enum gpio_signal signal)
{
	timestamp_t timelimit;
	int d = (signal == GPIO_LID_SWITCHn ? DEBOUNCE_LID : DEBOUNCE_PWRBTN);

	/* Set 30 ms debounce timelimit */
	timelimit = get_time();
	timelimit.val += 30000;

	/* Handle lid switch and power button debounce */
	debounce_isr[d].tstamp = timelimit;
	debounce_isr[d].started = 1;
}


int power_button_init(void)
{
	debounce_isr[DEBOUNCE_LID].started = 0;
	debounce_isr[DEBOUNCE_LID].callback = lid_switch_isr;
	debounce_isr[DEBOUNCE_PWRBTN].started = 0;
	debounce_isr[DEBOUNCE_PWRBTN].callback = power_button_isr;

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_POWER_BUTTONn);
	gpio_enable_interrupt(GPIO_LID_SWITCHn);

	return EC_SUCCESS;
}


void power_button_task(void)
{
	int i;
	timestamp_t ts;

	while (1) {
		usleep(1000);
		ts = get_time();
		for (i = 0; i < DEBOUNCE_ISR_ID_MAX; ++i) {
			if (debounce_isr[i].started &&
				ts.val >= debounce_isr[i].tstamp.val) {
				debounce_isr[i].started = 0;
				debounce_isr[i].callback();
			}
		}

		pwrbtn_sm_handle(ts);
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

	uart_printf("Simulating %d ms power button press.\n", ms);
	set_pwrbtn_to_pch(0);
	usleep(ms * 1000);
	set_pwrbtn_to_pch(1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn);
