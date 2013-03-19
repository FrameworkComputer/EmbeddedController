/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED state machine to drive RGB LED on LP5562
 */

#include "common.h"
#include "extpower.h"
#include "hooks.h"
#include "lp5562.h"
#include "pmu_tpschrome.h"
#include "smart_battery.h"

/* We use yellow LED instead of blue LED. Re-map colors here. */
#define LED_COLOR_NONE   LP5562_COLOR_NONE
#define LED_COLOR_GREEN  LP5562_COLOR_GREEN
#define LED_COLOR_YELLOW LP5562_COLOR_BLUE
#define LED_COLOR_RED    LP5562_COLOR_RED

/* LED breathing program */
static const uint8_t breathing_prog[] = {0x41, 0xff,  /* 0x80 -> 0x0 */
					 0x41, 0x7f,  /* 0x0 -> 0x80 */
					 0x7f, 0x00,  /* Wait ~4s */
					 0x7f, 0x00,
					 0x7f, 0x00,
					 0x7f, 0x00,
					 0x00, 0x00}; /* Repeat */

static int led_breathing(int enabled)
{
	int ret = 0;

	if (enabled) {
		ret |= lp5562_engine_load(LP5562_ENG_SEL_1,
					  breathing_prog,
					  sizeof(breathing_prog));
		ret |= lp5562_engine_control(LP5562_ENG_RUN,
					     LP5562_ENG_HOLD,
					     LP5562_ENG_HOLD);
		ret |= lp5562_set_engine(LP5562_ENG_SEL_NONE,
					 LP5562_ENG_SEL_NONE,
					 LP5562_ENG_SEL_1);
	} else {
		ret |= lp5562_engine_control(LP5562_ENG_HOLD,
					     LP5562_ENG_HOLD,
					     LP5562_ENG_HOLD);
		ret |= lp5562_set_engine(LP5562_ENG_SEL_NONE,
					 LP5562_ENG_SEL_NONE,
					 LP5562_ENG_SEL_NONE);
	}

	return ret;
}

static void battery_led_update(void)
{
	int current;
	int desired_current;

	/* Current states and next states */
	static uint32_t color = LED_COLOR_RED;
	static int breathing;
	static int led_power;
	int new_color = LED_COLOR_RED;
	int new_breathing = 0;
	int new_led_power;

	/* Determine LED power */
	new_led_power = extpower_is_present();
	if (new_led_power != led_power) {
		led_power = new_led_power;
		if (new_led_power) {
			lp5562_poweron();
		} else {
			color = LED_COLOR_NONE;
			if (breathing) {
				led_breathing(0);
				breathing = 0;
			}
			lp5562_poweroff();
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
		new_color = LED_COLOR_GREEN;
		break;
	case ST_DISCHARGING:
		/* Discharging with AC, must be battery assist */
		new_color = LED_COLOR_YELLOW;
		new_breathing = 1;
		break;
	case ST_PRE_CHARGING:
		new_color = LED_COLOR_YELLOW;
		break;
	case ST_CHARGING:
		if (battery_current(&current) ||
		    battery_desired_current(&desired_current)) {
			/* Cannot talk to the battery. Set LED to red. */
			new_color = LED_COLOR_RED;
			break;
		}

		if (current < 0 && desired_current > 0) { /* Battery assist */
			new_breathing = 1;
			new_color = LED_COLOR_YELLOW;
			break;
		}

		if (current && desired_current)
			new_color = LED_COLOR_YELLOW;
		else
			new_color = LED_COLOR_GREEN;
		break;
	case ST_CHARGING_ERROR:
		new_color = LED_COLOR_RED;
		break;
	}

	if (new_color != color) {
		lp5562_set_color(new_color);
		color = new_color;
	}
	if (new_breathing != breathing) {
		led_breathing(new_breathing);
		breathing = new_breathing;
	}
}
DECLARE_HOOK(HOOK_SECOND, battery_led_update, HOOK_PRIO_DEFAULT);
