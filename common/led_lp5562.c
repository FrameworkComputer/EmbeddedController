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
#include "timer.h"
#include "util.h"

#define GREEN_LED_THRESHOLD 94

/* Minimal interval between changing LED color to green and yellow. */
#define LED_WAIT_INTERVAL (15 * SECOND)

/* We use yellow LED instead of blue LED. Re-map colors here. */
#define LED_COLOR_NONE   LP5562_COLOR_NONE
#define LED_COLOR_GREEN  LP5562_COLOR_GREEN(0x10)
#define LED_COLOR_YELLOW LP5562_COLOR_BLUE(0x40)
#define LED_COLOR_RED    LP5562_COLOR_RED(0x80)

/* LED states */
enum led_state_t {
	LED_STATE_SOLID_RED,
	LED_STATE_SOLID_GREEN,
	LED_STATE_SOLID_YELLOW,

	/* Not an actual state */
	LED_STATE_OFF,
};

static enum led_state_t last_state = LED_STATE_OFF;
static int led_auto_control = 1;

static int set_led_color(enum led_state_t state)
{
	int rv = EC_SUCCESS;

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
		rv = lp5562_set_color(LED_COLOR_YELLOW);
		break;
	case LED_STATE_OFF:
		break;
	}

	if (rv == EC_SUCCESS)
		last_state = state;
	return rv;
}

/*****************************************************************************/
/* Host commands */

static int led_command_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_led_control *p = args->params;
	struct ec_response_led_control *r = args->response;
	int i;
	uint8_t clipped[EC_LED_COLOR_COUNT];

	/* Only support battery LED control */
	if (p->led_id != EC_LED_ID_BATTERY_LED)
		return EC_RES_INVALID_PARAM;

	if (p->flags & EC_LED_FLAGS_AUTO) {
		if (!extpower_is_present())
			lp5562_poweroff();
		last_state = LED_STATE_OFF;
		led_auto_control = 1;
	} else if (!(p->flags & EC_LED_FLAGS_QUERY)) {
		for (i = 0; i < EC_LED_COLOR_COUNT; ++i)
			clipped[i] = MIN(p->brightness[i], 0x80);
		led_auto_control = 0;
		if (!extpower_is_present())
			lp5562_poweron();
		if (lp5562_set_color((clipped[EC_LED_COLOR_RED] << 16) +
				     (clipped[EC_LED_COLOR_GREEN] << 8) +
				     clipped[EC_LED_COLOR_YELLOW]))
			return EC_RES_ERROR;
	}

	r->brightness_range[EC_LED_COLOR_RED] = 0x80;
	r->brightness_range[EC_LED_COLOR_GREEN] = 0x80;
	r->brightness_range[EC_LED_COLOR_BLUE] = 0x0;
	r->brightness_range[EC_LED_COLOR_YELLOW] = 0x80;
	r->brightness_range[EC_LED_COLOR_WHITE] = 0x0;
	args->response_size = sizeof(struct ec_response_led_control);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LED_CONTROL,
		     led_command_control,
		     EC_VER_MASK(1));

/*****************************************************************************/
/* Hooks */

static void battery_led_update(void)
{
	int current;
	int desired_current;
	int rv;
	int state_of_charge;
	enum led_state_t state = LED_STATE_OFF;

	/* Current states and next states */
	static int led_power = -1;
	int new_led_power;

	/*
	 * The time before which we should not change LED
	 * color between green and yellow.
	 */
	static timestamp_t led_update_deadline = {.val = 0};

	/* Determine LED power */
	new_led_power = extpower_is_present();
	if (new_led_power != led_power) {
		if (new_led_power) {
			rv = lp5562_poweron();
		} else {
			rv = lp5562_poweroff();
			set_led_color(LED_STATE_OFF);
			led_update_deadline.val = 0;
		}
		if (!rv)
			led_power = new_led_power;
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
		state = LED_STATE_SOLID_YELLOW;
		break;
	case ST_IDLE0:
	case ST_BAD_COND:
	case ST_PRE_CHARGING:
		state = LED_STATE_SOLID_YELLOW;
		break;
	case ST_CHARGING:
		if (battery_current(&current) ||
		    battery_desired_current(&desired_current) ||
		    battery_state_of_charge(&state_of_charge)) {
			/* Cannot talk to the battery. Set LED to red. */
			state = LED_STATE_SOLID_RED;
			break;
		}

		if (current < 0 && desired_current > 0) { /* Battery assist */
			state = LED_STATE_SOLID_YELLOW;
			break;
		}

		/* If battery doesn't want any current, it's considered full. */
		if (state_of_charge < GREEN_LED_THRESHOLD)
			state = LED_STATE_SOLID_YELLOW;
		else
			state = LED_STATE_SOLID_GREEN;
		break;
	case ST_CHARGING_ERROR:
		state = LED_STATE_SOLID_RED;
		break;
	}

	if (state == LED_STATE_SOLID_GREEN ||
			state == LED_STATE_SOLID_YELLOW) {
		if (!timestamp_expired(led_update_deadline, NULL))
			return;
		led_update_deadline.val =
			get_time().val + LED_WAIT_INTERVAL;
	} else {
		led_update_deadline.val = 0;
	}

	set_led_color(state);
}
DECLARE_HOOK(HOOK_SECOND, battery_led_update, HOOK_PRIO_DEFAULT);
