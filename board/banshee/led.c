/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Banshee specific PWM LED settings:
 * there are 2 LEDs on each side of the board,
 * each one can be controlled separately. The LED colors are white or amber,
 * and the default behavior is tied to the charging process: both sides are
 * amber while charging the battery and white when the battery is charged.
 */

#include <stdint.h>

#include "common.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "led_common.h"
#include "led_pwm.h"
#include "pwm.h"
#include "util.h"

#define LED_TICKS_PER_CYCLE 10
#define LED_ON_TICKS 5

#define BREATH_LIGHT_LENGTH	55
#define BREATH_HOLD_LENGTH	50
#define BREATH_OFF_LENGTH	200


const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};


enum breath_status {
	BREATH_LIGHT_UP = 0,
	BREATH_LIGHT_DOWN,
	BREATH_HOLD,
	BREATH_OFF,
};


const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = {  50,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0,  50,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0,   8 },
	[EC_LED_COLOR_YELLOW] = {  40,  50,   0 },
	[EC_LED_COLOR_WHITE]  = {  20,  50,  25 },
	[EC_LED_COLOR_AMBER]  = {  45,  5,    0 },
};

struct pwm_led_color_map pwr_led_color_map[EC_LED_COLOR_COUNT] = {
				/* White, Green, Red */
	[EC_LED_COLOR_WHITE]  = {  BREATH_LIGHT_LENGTH,   0,   0 },
};

/*
 * Three logical LEDs with red、 blue、green channels
 * and one logical LED with white channels.
 */
struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	[PWM_LED0] = {
		.ch0 = PWM_CH_SIDE_LED_R,
		.ch1 = PWM_CH_SIDE_LED_G,
		.ch2 = PWM_CH_SIDE_LED_B,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
	[PWM_LED1] = {
		.ch0 = PWM_CH_POWER_LED_W,
		.ch1 = PWM_LED_NO_CHANNEL,
		.ch2 = PWM_LED_NO_CHANNEL,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
};


uint8_t breath_led_light_up;
uint8_t breath_led_light_down;
uint8_t breath_led_hold;
uint8_t breath_led_off;


int breath_pwm_enable;
int breath_led_status;
static void breath_led_pwm_deferred(void);
DECLARE_DEFERRED(breath_led_pwm_deferred);

/*
 *	Breath LED API
 *	Max duty (percentage) = BREATH_LIGHT_LENGTH (55%)
 *	Fade time (second) = 550ms(In) / 550ms(Out)
 *	Duration time (second) = BREATH_HOLD_LENGTH(500ms)
 *	Interval time (second) = BREATH_OFF_LENGTH(2000ms)
 */

static void breath_led_pwm_deferred(void)
{

	switch (breath_led_status) {
	case BREATH_LIGHT_UP:

		if (breath_led_light_up <=  BREATH_LIGHT_LENGTH)
			pwm_set_duty(PWM_CH_POWER_LED_W,
					breath_led_light_up++);
		else {
			breath_led_light_up = 0;
			breath_led_light_down = BREATH_LIGHT_LENGTH;
			breath_led_status = BREATH_HOLD;
		}

		break;
	case BREATH_HOLD:

		if (breath_led_hold <=  BREATH_HOLD_LENGTH)
			breath_led_hold++;
		else {
			breath_led_hold = 0;
			breath_led_status = BREATH_LIGHT_DOWN;
		}

		break;
	case BREATH_LIGHT_DOWN:

		if (breath_led_light_down != 0)
			pwm_set_duty(PWM_CH_POWER_LED_W,
					breath_led_light_down--);
		else {
			breath_led_light_down = BREATH_LIGHT_LENGTH;
			breath_led_status = BREATH_OFF;
		}

		break;
	case BREATH_OFF:

		if (breath_led_off <=  BREATH_OFF_LENGTH)
			breath_led_off++;
		else {
			breath_led_off = 0;
			breath_led_status = BREATH_LIGHT_UP;
		}

		break;
	}


	if (breath_pwm_enable)
		hook_call_deferred(&breath_led_pwm_deferred_data, 10 * MSEC);
}


void breath_led_run(uint8_t enable)
{
	if (enable && !breath_pwm_enable) {
		breath_pwm_enable = true;
		breath_led_status = BREATH_LIGHT_UP;
		hook_call_deferred(&breath_led_pwm_deferred_data, 10 * MSEC);
	} else if (!enable && breath_pwm_enable) {
		breath_pwm_enable = false;
		breath_led_light_up = 0;
		breath_led_light_down = 0;
		breath_led_hold = 0;
		breath_led_off = 0;
		breath_led_status = BREATH_OFF;
		hook_call_deferred(&breath_led_pwm_deferred_data, -1);
	}
}


void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 100;
		brightness_range[EC_LED_COLOR_GREEN] = 100;
		brightness_range[EC_LED_COLOR_YELLOW] = 100;
		brightness_range[EC_LED_COLOR_AMBER] = 100;
		brightness_range[EC_LED_COLOR_BLUE] = 100;
		brightness_range[EC_LED_COLOR_WHITE] = 100;
	} else if (led_id == EC_LED_ID_POWER_LED)
		brightness_range[EC_LED_COLOR_WHITE] = 100;

}

void set_pwr_led_color(enum pwm_led_id id, int color)
{
	struct pwm_led duty = { 0 };
	const struct pwm_led *led = &pwm_leds[id];

	if ((id >= CONFIG_LED_PWM_COUNT) || (id < 0) ||
	    (color >= EC_LED_COLOR_COUNT) || (color < -1))
		return;

	if (color != -1) {
		duty.ch0 = pwr_led_color_map[color].ch0;
		duty.ch1 = pwr_led_color_map[color].ch1;
		duty.ch2 = pwr_led_color_map[color].ch2;
	}

	if (led->ch0 != (enum pwm_channel)PWM_LED_NO_CHANNEL)
		led->set_duty(led->ch0, duty.ch0);
	if (led->ch1 != (enum pwm_channel)PWM_LED_NO_CHANNEL)
		led->set_duty(led->ch1, duty.ch1);
	if (led->ch2 != (enum pwm_channel)PWM_LED_NO_CHANNEL)
		led->set_duty(led->ch2, duty.ch2);
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	enum pwm_led_id pwm_id;

	/* Convert ec_led_id to pwm_led_id. */
	if (led_id == EC_LED_ID_BATTERY_LED)
		pwm_id = PWM_LED0;
	else if (led_id == EC_LED_ID_POWER_LED)
		pwm_id = PWM_LED1;
	else
		return EC_ERROR_UNKNOWN;

	if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_WHITE);
		else
			/* Otherwise, the "color" is "off". */
			set_pwr_led_color(pwm_id, -1);
	} else {
		if (brightness[EC_LED_COLOR_RED])
			set_pwm_led_color(pwm_id, EC_LED_COLOR_RED);
		else if (brightness[EC_LED_COLOR_GREEN])
			set_pwm_led_color(pwm_id, EC_LED_COLOR_GREEN);
		else if (brightness[EC_LED_COLOR_BLUE])
			set_pwm_led_color(pwm_id, EC_LED_COLOR_BLUE);
		else if (brightness[EC_LED_COLOR_YELLOW])
			set_pwm_led_color(pwm_id, EC_LED_COLOR_YELLOW);
		else if (brightness[EC_LED_COLOR_WHITE])
			set_pwm_led_color(pwm_id, EC_LED_COLOR_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER])
			set_pwm_led_color(pwm_id, EC_LED_COLOR_AMBER);
		else
			/* Otherwise, the "color" is "off". */
			set_pwm_led_color(pwm_id, -1);
	}

	return EC_SUCCESS;
}

static void select_active_port_led(int port)
{
	if ((port == USBC_PORT_C0) || (port == USBC_PORT_C1)) {
		gpio_set_level(GPIO_LEFT_SIDE, 0);
		gpio_set_level(GPIO_RIGHT_SIDE, 1);
	} else if ((port == USBC_PORT_C2) || (port == USBC_PORT_C3)) {
		gpio_set_level(GPIO_LEFT_SIDE, 1);
		gpio_set_level(GPIO_RIGHT_SIDE, 0);
	} else if ((charge_get_state() == PWR_STATE_DISCHARGE &&
			 charge_get_percent() < 10) ||
			 charge_get_state() == PWR_STATE_ERROR) {
		gpio_set_level(GPIO_LEFT_SIDE, 1);
		gpio_set_level(GPIO_RIGHT_SIDE, 1);
	} else {
		gpio_set_level(GPIO_LEFT_SIDE, 0);
		gpio_set_level(GPIO_RIGHT_SIDE, 0);
	}
}

static void set_active_port_color(int color)
{
	int port = charge_manager_get_active_charge_port();

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		select_active_port_led(port);
		set_pwm_led_color(PWM_LED0, port ? color : -1);
	}
}

static void led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(EC_LED_COLOR_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
			if (charge_get_percent() < 10)
				set_active_port_color((battery_ticks & 0x2) ?
					EC_LED_COLOR_RED : -1);
			else
				set_active_port_color(-1);
		}
		break;
	case PWR_STATE_ERROR:
		set_active_port_color((battery_ticks & 0x2) ?
				EC_LED_COLOR_RED : -1);
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(EC_LED_COLOR_GREEN);
		break;
	case PWR_STATE_IDLE:
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			set_active_port_color((battery_ticks & 0x4) ?
					EC_LED_COLOR_AMBER : -1);
		else
			set_active_port_color(EC_LED_COLOR_AMBER);
		break;
	default:
		break;
	}

}

static void led_set_power(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		breath_led_run(1);
		return;
	}
	else
		breath_led_run(0);

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		set_pwr_led_color(PWM_LED1, EC_LED_COLOR_WHITE);
	} else
		set_pwr_led_color(PWM_LED1, -1);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();

}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
