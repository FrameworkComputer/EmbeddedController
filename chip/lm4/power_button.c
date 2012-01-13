/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button and lid switch module for Chrome EC */

#include "gpio.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"
#include "uart.h"

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
	PWRBTN_STATE_START = 1,
	PWRBTN_STATE_T0 = 2,
	PWRBTN_STATE_T1 = 3,
	PWRBTN_STATE_T2 = 4,
	PWRBTN_STATE_STOPPING = 5,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_STOPPED;
/* The next timestamp to move onto next state if power button is still pressed.
 */
static timestamp_t pwrbtn_next_ts = {0};

#define PWRBTN_DELAY_T0 32000  /* 32ms */
#define PWRBTN_DELAY_T1 (4000000 - PWRBTN_DELAY_T0)  /* 4 secs - t0 */
#define PWRBTN_DELAY_T2 4000000  /* 4 secs */


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
 *                t0    t1       t2
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
	if (pwrbtn_state == PWRBTN_STATE_STOPPED ||
	    current.val < pwrbtn_next_ts.val)
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
		pwrbtn_next_ts.val = current.val + PWRBTN_DELAY_T2;
		pwrbtn_state = PWRBTN_STATE_T2;
		set_pwrbtn_to_pch(0);
		break;
	case PWRBTN_STATE_T2:
		/* T2 has passed */
	case PWRBTN_STATE_STOPPING:
		set_pwrbtn_to_pch(1);
		pwrbtn_state = PWRBTN_STATE_STOPPED;
		break;
	default:
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

