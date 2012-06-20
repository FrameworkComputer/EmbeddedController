/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * GAIA SoC power sequencing module for Chrome EC
 *
 * This implements the following features:
 *
 * - Cold reset powers off the AP
 *
 *  When powered off:
 *  - Press pwron turns on the AP
 *  - Hold pwron turns on the AP, and then 16s later turns it off and leaves
 *  it off until pwron is released and pressed again
 *
 *  When powered on:
 *  - The PMIC PWRON signal is released one second after the power button is
 *  released (we expect that U-Boot as asserted XPSHOLD by then)
 *  - Holding pwron for 8s powers off the AP
 *  - Pressing and releasing pwron within that 8s is ignored
 *  - If XPSHOLD is dropped by the AP, then we power the AP off
 */

#include "board.h"
#include "chipset.h"  /* This module implements chipset functions too */
#include "console.h"
#include "gpio.h"
#include "keyboard_scan.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/* Time necessary for the 5v regulator output to stabilize */
#define DELAY_5V_SETUP        1000  /* 1ms */

/* Delay between 1.35v and 3.3v rails startup */
#define DELAY_RAIL_STAGGERING 100  /* 100us */

/* Long power key press to force shutdown */
#define DELAY_FORCE_SHUTDOWN  8000000 /* 8s */

/*
 * If the power key is pressed to turn on, then held for this long, we
 * power off.
 *
 * The idea here is that behavior for 8s for AP shutdown is unchanged
 * but power-on is modified to allow enough time U-Boot to be updated
 * via USB (which takes about 10sec).
 *
 * So after power button is pressed:

 * Normal case: User releases power button and gaia_power_task() goes
 *    into the inner loop, waiting for next event to occur (power button
 *    press or XPSHOLD == 0).
 *
 * U-Boot updating: User presses and holds power button. EC does not
 *    check XPSHOLD, and waits up to 16sec for an event. If no event occurs
 *    within 16sec, EC powers off AP.
 */
#define DELAY_SHUTDOWN_ON_POWER_HOLD	(16 * 1000000)

/* Delay after power button release before we release GPIO_PMIC_PWRON_L */
#define DELAY_RELEASE_PWRON	1000000 /* 1s */

/* debounce time to prevent accidental power-on after keyboard power off */
#define KB_PWR_ON_DEBOUNCE    250    /* 250us */

/* PMIC fails to set the LDO2 output */
#define PMIC_TIMEOUT          100000  /* 100ms */

/* Default timeout for input transition */
#define FAIL_TIMEOUT          500000 /* 500ms */


/* Application processor power state */
static int ap_on;
static int ap_suspended;

/* simulated event state */
static int force_signal = -1;
static int force_value;

/* 1 if the power button was pressed last time we checked */
static char power_button_was_pressed;

/* time where we will power off, if power button still held down */
static timestamp_t power_off_deadline;

/* 1 if we have released GPIO_PMIC_PWRON_L */
static int pwron_released;

/* time where we will release GPIO_PMIC_PWRON_L */
static timestamp_t pwron_deadline;

/* force AP power on (used for recovery keypress) */
static int auto_power_on;

/*****************************************************************************/
/* Keyboard power button LED state machine */

/* Suspend mode blinking duty cycle */
#define LED_OFF_PERIOD  1500000
#define LED_ON_PERIOD     50000

/* GPIO level for each LED state : it's an active low output */
#define LED_GPIO_ON   0
#define LED_GPIO_OFF  1

/* power LED FSM states */
static enum power_led_state {
	POWER_LED_OFF,
	POWER_LED_ON,
	POWER_LED_SUSP0,
	POWER_LED_SUSP1,

	POWER_LED_STATE_COUNT
} led_state;

/* power LED FSM parameters for each state */
static const struct {
	int gpio;
	int duration;
	enum power_led_state next;
} led_config[POWER_LED_STATE_COUNT] = {
	[POWER_LED_OFF]   = {LED_GPIO_OFF, -1,             POWER_LED_OFF},
	[POWER_LED_ON]    = {LED_GPIO_ON,  -1,             POWER_LED_ON},
	[POWER_LED_SUSP0] = {LED_GPIO_OFF, LED_OFF_PERIOD, POWER_LED_SUSP1},
	[POWER_LED_SUSP1] = {LED_GPIO_ON,  LED_ON_PERIOD,  POWER_LED_SUSP0},
};

static void set_power_led(enum power_led_state new_state)
{
	led_state = new_state;
	/* Wake up the task */
	task_wake(TASK_ID_POWERLED);
}

void power_led_task(void)
{
	while (1) {
		int state_timeout;

		gpio_set_level(GPIO_POWER_LED_L, led_config[led_state].gpio);
		state_timeout = led_config[led_state].duration;
		led_state = led_config[led_state].next;
		task_wait_event(state_timeout);
	}
}

/*****************************************************************************/

/*
 * Wait for GPIO "signal" to reach level "value".
 * Returns EC_ERROR_TIMEOUT if timeout before reaching the desired state.
 *
 * @param signal	Signal to watch
 * @param value		Value to watch for
 * @param timeout	Timeout in microseconds from now, or -1 to wait forever
 * @return 0 if signal did change to required value, EC_ERROR_TIMEOUT if we
 * timed out first.
 */
static int wait_in_signal(enum gpio_signal signal, int value, int timeout)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + timeout;

	while (((force_signal != signal) || (force_value != value)) &&
			gpio_get_level(signal) != value) {
		now = get_time();
		if (timeout < 0) {
			task_wait_event(-1);
		} else if (timestamp_expired(deadline, &now) ||
				(task_wait_event(deadline.val - now.val) ==
					TASK_EVENT_TIMER)) {
			CPRINTF("Timeout waiting for GPIO %d/%s\n", signal,
				    gpio_get_name(signal));
			return EC_ERROR_TIMEOUT;
		}
	}

	return EC_SUCCESS;
}

/*
 * Check for some event triggering the shutdown.
 *
 * It can be either a long power button press or a shutdown triggered from the
 * AP and detected by reading XPSHOLD.
 *
 * @return 1 if a shutdown should happen, 0 if not
 */
static int check_for_power_off_event(void)
{
	timestamp_t now;
	int pressed = 0;

	/* Check for power button press */
	if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0) {
		udelay(KB_PWR_ON_DEBOUNCE);
		if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0)
			pressed = 1;
	}

	now = get_time();
	if (pressed) {
		gpio_set_level(GPIO_PMIC_PWRON_L, 0);

		if (!power_button_was_pressed) {
			power_off_deadline.val = now.val + DELAY_FORCE_SHUTDOWN;
			CPRINTF("Waiting for long power press %u\n",
				power_off_deadline.le.lo);
		} else if (timestamp_expired(power_off_deadline, &now)) {
			power_off_deadline.val = 0;
			CPRINTF("Power off after long press now=%u, %u\n",
				now.le.lo, power_off_deadline.le.lo);
			return 2;
		}
	} else if (power_button_was_pressed) {
		CPUTS("Cancel power off\n");
		gpio_set_level(GPIO_PMIC_PWRON_L, 1);
	}
	power_button_was_pressed = pressed;

	/* XPSHOLD released by AP : shutdown immediately */
	if (pwron_released && gpio_get_level(GPIO_SOC1V8_XPSHOLD) == 0)
		return 3;
	return 0;
}

void gaia_suspend_event(enum gpio_signal signal)
{
	if (!ap_on) /* power on/off : not a real suspend / resume */
		return;

	ap_suspended = !gpio_get_level(GPIO_SUSPEND_L);

	set_power_led(ap_suspended ? POWER_LED_SUSP0 : POWER_LED_ON);
}

void gaia_power_event(enum gpio_signal signal)
{
	/* Wake up the task */
	task_wake(TASK_ID_GAIAPOWER);
}

int gaia_power_init(void)
{
	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_KB_PWR_ON_L);
	gpio_enable_interrupt(GPIO_PP1800_LDO2);
	gpio_enable_interrupt(GPIO_SOC1V8_XPSHOLD);

	/* auto power on if the recovery combination was pressed */
	if (keyboard_scan_recovery_pressed())
		auto_power_on = 1;

	return EC_SUCCESS;
}


/*****************************************************************************/
/* Chipset interface */

int chipset_in_state(int state_mask)
{
	/* If AP is off, match any off state for now */
	if ((state_mask & CHIPSET_STATE_ANY_OFF) && !ap_on)
		return 1;

	/* If AP is on, match on state */
	if ((state_mask & CHIPSET_STATE_ON) && ap_on && !ap_suspended)
		return 1;

	/* if AP is suspended, match on state */
	if ((state_mask & CHIPSET_STATE_SUSPEND) && ap_on && ap_suspended)
		return 1;

	/* In any other case, we don't have a match */
	return 0;
}

void chipset_exit_hard_off(void)
{
	/* TODO: implement, if/when we take the AP down to a hard-off state */
}

/*****************************************************************************/

/**
 * Check if there has been a power-on event
 *
 * This waits for the power button to be pressed, then returns whether it
 * is still pressed, after a debounce period
 *
 * @return 1 if there has been a power-on event, 0 if not
 */
static int check_for_power_on_event(void)
{
	/* the system is already ON */
	if (gpio_get_level(GPIO_EN_PP3300))
		return 1;

	/* power on requested at EC startup for recovery */
	if (auto_power_on) {
		auto_power_on = 0;
		return 1;
	}

	/* wait for Power button press */
	wait_in_signal(GPIO_KB_PWR_ON_L, 0, -1);

	udelay(KB_PWR_ON_DEBOUNCE);
	return gpio_get_level(GPIO_KB_PWR_ON_L) == 0;
}

/**
 * Power on the AP
 *
 * @return 0 if ok, -1 on error (PP1800_LDO2 failed to come on)
 */
static int power_on(void)
{
	/* Enable 5v power rail */
	gpio_set_level(GPIO_EN_PP5000, 1);
	/* wait to have stable power */
	usleep(DELAY_5V_SETUP);

	/* Startup PMIC */
	gpio_set_level(GPIO_PMIC_PWRON_L, 0);
	/* wait for all PMIC regulators to be ready */
	wait_in_signal(GPIO_PP1800_LDO2, 1, PMIC_TIMEOUT);

	/*
	 * If PP1800_LDO2 did not come up (e.g. PMIC_TIMEOUT was reached),
	 * turn off 5v rail and start over.
	 */
	if (gpio_get_level(GPIO_PP1800_LDO2) == 0) {
		gpio_set_level(GPIO_EN_PP5000, 0);
		usleep(DELAY_5V_SETUP);
		CPUTS("Fatal error: PMIC failed to enable\n");
		return -1;
	}

	/* Enable DDR 1.35v power rail */
	gpio_set_level(GPIO_EN_PP1350, 1);
	/* wait to avoid large inrush current */
	usleep(DELAY_RAIL_STAGGERING);
	/* Enable 3.3v power rail */
	gpio_set_level(GPIO_EN_PP3300, 1);
	ap_on = 1;
	set_power_led(POWER_LED_ON);
	CPUTS("AP running ...\n");
	return 0;
}

/**
 * Wait for the power button to be released
 *
 * @return 0 if ok, -1 if power button failed to release
 */
static int wait_for_power_button_release(unsigned int timeout_us)
{
	/* wait for Power button release */
	wait_in_signal(GPIO_KB_PWR_ON_L, 1, timeout_us);

	udelay(KB_PWR_ON_DEBOUNCE);
	if (gpio_get_level(GPIO_KB_PWR_ON_L) == 0) {
		CPUTS("Power button was not released in time\n");
		return -1;
	}
	CPUTS("Power button released\n");
	return 0;
}

/**
 * Power off the AP
 */
static void power_off(void)
{
	/* switch off all rails */
	gpio_set_level(GPIO_EN_PP3300, 0);
	gpio_set_level(GPIO_EN_PP1350, 0);
	gpio_set_level(GPIO_PMIC_PWRON_L, 1);
	gpio_set_level(GPIO_EN_PP5000, 0);
	ap_on = 0;
	set_power_led(POWER_LED_OFF);
	CPUTS("Shutdown complete.\n");
}

/**
 * Set a timer to release GPIO_PMIC_PWRON_L in the future
 */
static void set_pwron_timer(void)
{
	pwron_deadline = get_time();
	pwron_deadline.val += DELAY_RELEASE_PWRON;
	CPRINTF("Setting pwron timer %d\n", pwron_deadline.val);
	pwron_released = 0;
}

static void check_pwron_release(void)
{
	if (!pwron_released && timestamp_expired(pwron_deadline, NULL)) {
		pwron_deadline.val = 0;
		pwron_released = 1;
		gpio_set_level(GPIO_PMIC_PWRON_L, 1);
		CPRINTF("Releasing pwron\n");
	}
}

/*
 * Calculates the delay in microseconds to the next time we have to check
 * for a power event,
 *
  *@return delay to next check, or -1 if no future check is needed
 */
static int next_pwr_event(void)
{
	uint64_t next;

	if (!pwron_deadline.val && !power_off_deadline.val)
		return -1;

	/* We know that pwron_deadline will be earlier, if it exists */
	next = pwron_deadline.val ? pwron_deadline.val
			: power_off_deadline.val;
	return next - get_time().val;
}


/*****************************************************************************/

void gaia_power_task(void)
{
	int value;

	gaia_power_init();
	ap_on = 0;

	while (1) {
		/* Wait until we need to power on, then power on */
		while (!check_for_power_on_event())
			task_wait_event(-1);

		/*
		 * If we can power on, and the power button is released,
		 * start running!
		 */
		if (!power_on() && !wait_for_power_button_release(
					DELAY_SHUTDOWN_ON_POWER_HOLD)) {
			/* Wait until we need to power off, then power off */
			power_button_was_pressed = 0;
			set_pwron_timer();
			while (value = check_for_power_off_event(), !value) {
				check_pwron_release();
				task_wait_event(next_pwr_event());
			}
			CPRINTF("ending loop %d\n", value);
		}
		power_off();
		wait_for_power_button_release(-1);
	}
}

/*****************************************************************************/
/* Console debug command */

static int command_force_power(int argc, char **argv)
{
	/* simulate power button pressed */
	force_signal = GPIO_KB_PWR_ON_L;
	force_value = 1;
	/* Wake up the task */
	task_wake(TASK_ID_GAIAPOWER);
	/* wait 100 ms */
	usleep(100000);
	/* release power button */
	force_signal = -1;
	force_value = 0;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forcepower, command_force_power,
			NULL,
			"Force power on",
			NULL);
