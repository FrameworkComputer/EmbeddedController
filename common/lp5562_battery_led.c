/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED state machine to drive RGB LED on LP5562
 */

#include "common.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "lp5562.h"
#include "pmu_tpschrome.h"
#include "smart_battery.h"
#include "util.h"

/* We use yellow LED instead of blue LED. Re-map colors here. */
#define LED_COLOR_NONE   LP5562_COLOR_NONE
#define LED_COLOR_GREEN  LP5562_COLOR_GREEN
#define LED_COLOR_YELLOW LP5562_COLOR_BLUE
#define LED_COLOR_RED    LP5562_COLOR_RED

/* LED states */
enum led_state_t {
	LED_STATE_SOLID_RED,
	LED_STATE_SOLID_GREEN,
	LED_STATE_SOLID_YELLOW,
	LED_STATE_TRANSITION_ON,  /* Solid yellow -> breathing */
	LED_STATE_TRANSITION_OFF, /* Breathing -> solid yellow */
	LED_STATE_BREATHING,

	/* Not an actual state */
	LED_STATE_OFF,
};

/* LED breathing program */
static const uint8_t breathing_prog[] = {0x41, 0xff,  /* 0x80 -> 0x0 */
					 0x41, 0x7f,  /* 0x0 -> 0x80 */
					 0x7f, 0x00,  /* Wait ~4s */
					 0x7f, 0x00,
					 0x7f, 0x00,
					 0x7f, 0x00,
					 0x00, 0x00,  /* Go to start */
					 0x40, 0x80,  /* Set PWM = 0x80 */
					 0x00, 0x00}; /* Go to start */
#define BREATHING_PROG_ENTRY 7

static enum led_state_t last_state = LED_STATE_OFF;
static int led_auto_control = 1;

static int stop_led_engine(void)
{
	int pc;
	if (lp5562_get_engine_state(LP5562_ENG_SEL_1) == LP5562_ENG_STEP)
		return 0; /* Not stopped */
	pc = lp5562_get_pc(LP5562_ENG_SEL_1);
	if (pc == 1) {
		/* LED currently off. Ramp up. */
		lp5562_engine_control(LP5562_ENG_STEP,
				      LP5562_ENG_HOLD,
				      LP5562_ENG_HOLD);
		return 0;
	}

	lp5562_set_engine(LP5562_ENG_SEL_NONE,
			  LP5562_ENG_SEL_NONE,
			  LP5562_ENG_SEL_NONE);
	lp5562_set_color(LED_COLOR_YELLOW);
	return 1;
}

static int set_led_color(enum led_state_t state)
{
	int rv;

	ASSERT(state != LED_STATE_TRANSITION_ON &&
	       state != LED_STATE_TRANSITION_OFF);

	if (!led_auto_control || state == last_state)
		return EC_SUCCESS;

	switch (state) {
	case LED_STATE_SOLID_RED:
		rv = lp5562_set_color(LED_COLOR_RED);
		break;
	case LED_STATE_SOLID_GREEN:
		rv = lp5562_set_color(LED_COLOR_GREEN);
		break;
	case LED_STATE_SOLID_YELLOW:
	case LED_STATE_BREATHING:
		rv = lp5562_set_color(LED_COLOR_YELLOW);
		break;
	default:
		rv = EC_ERROR_UNKNOWN;
	}

	if (rv == EC_SUCCESS)
		last_state = state;
	return rv;
}

static void stablize_led(enum led_state_t desired_state)
{
	static enum led_state_t current_state = LED_STATE_OFF;
	enum led_state_t next_state = LED_STATE_OFF;

	/* TRANSITIONs are internal states */
	ASSERT(desired_state != LED_STATE_TRANSITION_ON &&
	       desired_state != LED_STATE_TRANSITION_OFF);

	if (desired_state == LED_STATE_OFF) {
		current_state = LED_STATE_OFF;
		return;
	}

	/* Determine next state */
	switch (current_state) {
	case LED_STATE_OFF:
	case LED_STATE_SOLID_RED:
	case LED_STATE_SOLID_GREEN:
		if (desired_state == LED_STATE_BREATHING)
			next_state = LED_STATE_SOLID_YELLOW;
		else
			next_state = desired_state;
		set_led_color(next_state);
		break;
	case LED_STATE_SOLID_YELLOW:
		if (desired_state == LED_STATE_BREATHING) {
			next_state = LED_STATE_TRANSITION_ON;
			lp5562_set_pc(LP5562_ENG_SEL_1, BREATHING_PROG_ENTRY);
			lp5562_engine_control(LP5562_ENG_STEP,
					      LP5562_ENG_HOLD,
					      LP5562_ENG_HOLD);
		} else {
			next_state = desired_state;
			set_led_color(next_state);
		}
		break;
	case LED_STATE_BREATHING:
		if (desired_state != LED_STATE_BREATHING) {
			next_state = LED_STATE_TRANSITION_OFF;
			lp5562_engine_control(LP5562_ENG_STEP,
					      LP5562_ENG_HOLD,
					      LP5562_ENG_HOLD);
		} else {
			next_state = LED_STATE_BREATHING;
		}
		break;
	case LED_STATE_TRANSITION_ON:
		if (desired_state == LED_STATE_BREATHING) {
			next_state = LED_STATE_BREATHING;
			lp5562_set_engine(LP5562_ENG_SEL_NONE,
					  LP5562_ENG_SEL_NONE,
					  LP5562_ENG_SEL_1);
			lp5562_engine_control(LP5562_ENG_RUN,
					      LP5562_ENG_HOLD,
					      LP5562_ENG_HOLD);
		} else {
			next_state = LED_STATE_SOLID_YELLOW;
			lp5562_engine_control(LP5562_ENG_HOLD,
					      LP5562_ENG_HOLD,
					      LP5562_ENG_HOLD);
		}
		break;
	case LED_STATE_TRANSITION_OFF:
		if (stop_led_engine())
			next_state = LED_STATE_SOLID_YELLOW;
		else
			next_state = LED_STATE_TRANSITION_OFF;
		break;
	}

	current_state = next_state;
}

/*****************************************************************************/
/* Host commands */

static int led_command_set(struct host_cmd_handler_args *args)
{
	const struct ec_params_led_set *p = args->params;

	if (p->flags & EC_LED_FLAGS_AUTO) {
		if (!extpower_is_present())
			lp5562_poweroff();
		last_state = LED_STATE_OFF;
		led_auto_control = 1;
	} else {
		led_auto_control = 0;
		if (!extpower_is_present())
			lp5562_poweron();
		if (lp5562_set_color((p->r << 16) + (p->g << 8) + p->b))
			return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LED_SET,
		     led_command_set,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Hooks */

static void battery_led_update(void)
{
	int current;
	int desired_current;
	enum led_state_t state = LED_STATE_OFF;

	/* Current states and next states */
	static int led_power = -1;
	int new_led_power;

	/* Determine LED power */
	new_led_power = extpower_is_present();
	if (new_led_power != led_power) {
		led_power = new_led_power;
		if (new_led_power) {
			lp5562_poweron();
			lp5562_engine_load(LP5562_ENG_SEL_1,
					   breathing_prog,
					   sizeof(breathing_prog));
		} else {
			lp5562_poweroff();
			stablize_led(LED_STATE_OFF);
		}
	}
	if (!new_led_power)
		return;

	/*
	 * LED power is controlled by accessory detection. We only
	 * set color here.
	 */
	switch (charge_get_state()) {
	case ST_IDLE:
		state = LED_STATE_SOLID_GREEN;
		break;
	case ST_DISCHARGING:
		/* Discharging with AC, must be battery assist */
		state = LED_STATE_BREATHING;
		break;
	case ST_IDLE0:
	case ST_BAD_COND:
	case ST_PRE_CHARGING:
		state = LED_STATE_SOLID_YELLOW;
		break;
	case ST_CHARGING:
		if (battery_current(&current) ||
		    battery_desired_current(&desired_current)) {
			/* Cannot talk to the battery. Set LED to red. */
			state = LED_STATE_SOLID_RED;
			break;
		}

		if (current < 0 && desired_current > 0) { /* Battery assist */
			state = LED_STATE_BREATHING;
			break;
		}

		if (current && desired_current)
			state = LED_STATE_SOLID_YELLOW;
		else
			state = LED_STATE_SOLID_GREEN;
		break;
	case ST_CHARGING_ERROR:
		state = LED_STATE_SOLID_RED;
		break;
	}

	stablize_led(state);
}
DECLARE_HOOK(HOOK_SECOND, battery_led_update, HOOK_PRIO_DEFAULT);
