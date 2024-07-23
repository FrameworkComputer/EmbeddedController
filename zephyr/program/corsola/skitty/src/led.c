/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_led.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "timer.h"
#include "util.h"

#include <stdint.h>

#include <zephyr/drivers/pwm.h>

#define BATT_LOW_BCT 8

#define LED_TICKS_PER_CYCLE 4
#define LED_TICKS_PER_CYCLE_S3 4
#define LED_ON_TICKS 2
#define POWER_LED_ON_S3_TICKS 2

#define LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(324)

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

#ifdef TEST_BUILD
int ztest_duty_white;
int ztest_duty_amber;
#endif /* TEST_BUILD */

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

static const struct board_led_pwm_dt_channel battery_amber_led =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(
		DT_NODELABEL(pwm_battery_amber_led));

static const struct board_led_pwm_dt_channel battery_white_led =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(
		DT_NODELABEL(pwm_battery_white_led));

static void led_pwm_set_duty(const struct board_led_pwm_dt_channel *ch,
			     int percent)
{
	uint32_t pulse_ns;
	int rv;

#ifdef TEST_BUILD
	if (ch->channel == battery_white_led.channel) {
		ztest_duty_white = percent;
	} else {
		ztest_duty_amber = percent;
	}
#endif

	if (!device_is_ready(ch->dev)) {
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(LED_PWM_PERIOD_NS * percent, 100);

	rv = pwm_set(ch->dev, ch->channel, LED_PWM_PERIOD_NS, pulse_ns,
		     ch->flags);
}

static int led_set_color_battery_duty(enum led_color color, int duty)
{
	/* Battery LED duty range from 0% ~ 100% */
	if (duty < 0 || 100 < duty)
		return EC_ERROR_UNKNOWN;

	switch (color) {
	case LED_WHITE:
		led_pwm_set_duty(&battery_white_led, duty);
		led_pwm_set_duty(&battery_amber_led, 0);
		break;
	case LED_AMBER:
		led_pwm_set_duty(&battery_white_led, 0);
		led_pwm_set_duty(&battery_amber_led, duty);
		break;
	case LED_OFF:
		led_pwm_set_duty(&battery_white_led, 0);
		led_pwm_set_duty(&battery_amber_led, 0);
		break;
	default:
		break;
	}

	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		break;
	default:
		break;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	int rv = EC_SUCCESS;

	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery_duty(
				LED_WHITE, brightness[EC_LED_COLOR_WHITE]);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery_duty(
				LED_AMBER, brightness[EC_LED_COLOR_AMBER]);
		else
			led_set_color_battery_duty(LED_OFF, 0);
		break;
	default:
		rv = EC_ERROR_PARAM1;
	}

	return rv;
}

static struct {
	uint32_t interval;
	int duty_inc;
	enum led_color color;
	uint32_t on_time;
	int duty;
} batt_led_pulse;

static void battery_set_pwm_led_tick(void);
DECLARE_DEFERRED(battery_set_pwm_led_tick);

#define BATT_LOW_LED_PULSE_MS (875 * MSEC)
#define BATT_CRI_LED_PULSE_MS (375 * MSEC)
#define BATT_LED_ON_TIME_MS (125 * MSEC)
#define BATT_LED_PULSE_TICK_MS (25 * MSEC)

#define BATT_LOW_LED_CONFIG_TICK(interval, color)                        \
	batt_led_config_tick(                                            \
		(interval),                                              \
		DIV_ROUND_UP(100, (BATT_LOW_LED_PULSE_MS / (interval))), \
		(color), (BATT_LED_ON_TIME_MS))

#define BATT_CRI_LED_CONFIG_TICK(interval, color)                        \
	batt_led_config_tick(                                            \
		(interval),                                              \
		DIV_ROUND_UP(100, (BATT_CRI_LED_PULSE_MS / (interval))), \
		(color), (BATT_LED_ON_TIME_MS))

static void batt_led_config_tick(uint32_t interval, int duty_inc,
				 enum led_color color, uint32_t on_time)
{
	batt_led_pulse.interval = interval;
	batt_led_pulse.duty_inc = duty_inc;
	batt_led_pulse.color = color;
	batt_led_pulse.on_time = on_time;
	batt_led_pulse.duty = 0;
}

__overridable enum led_pwr_state skitty_led_pwr_get_state()
{
	return led_pwr_get_state();
}

__overridable int skitty_charge_get_percent()
{
	return charge_get_percent();
}

static void led_set_battery(void)
{
	static unsigned int battery_ticks;
	static bool battery_low_triggeied = 0;
	static bool battery_critical_triggeied = 0;
	battery_ticks++;

	switch (skitty_led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		/* Always indicate when charging, even in suspend. */
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
			led_set_color_battery_duty(LED_AMBER, 100);
		break;
	case LED_PWRS_DISCHARGE:
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
			if (skitty_charge_get_percent() <=
				    BATTERY_LEVEL_CRITICAL &&
			    !battery_critical_triggeied) {
				battery_low_triggeied = 0;
				battery_critical_triggeied = 1;
				BATT_CRI_LED_CONFIG_TICK(BATT_LED_PULSE_TICK_MS,
							 LED_AMBER);
				hook_call_deferred(
					&battery_set_pwm_led_tick_data, 0);
			} else if (skitty_charge_get_percent() <=
					   BATT_LOW_BCT &&
				   skitty_charge_get_percent() >
					   BATTERY_LEVEL_CRITICAL &&
				   !battery_low_triggeied) {
				battery_critical_triggeied = 0;
				battery_low_triggeied = 1;
				BATT_LOW_LED_CONFIG_TICK(BATT_LED_PULSE_TICK_MS,
							 LED_AMBER);
				hook_call_deferred(
					&battery_set_pwm_led_tick_data, 0);
			} else if (skitty_charge_get_percent() > BATT_LOW_BCT &&
				   !battery_critical_triggeied &&
				   !battery_low_triggeied) {
				battery_low_triggeied = 0;
				battery_critical_triggeied = 0;
				led_set_color_battery_duty(LED_OFF, 0);
			}
		}
		break;
	case LED_PWRS_ERROR:
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
			led_set_color_battery_duty(
				(battery_ticks & 0x1) ? LED_AMBER : LED_OFF,
				(battery_ticks & 0x1) ? 100 : 0);
		}
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
			led_set_color_battery_duty(LED_WHITE, 100);
		break;
	case LED_PWRS_IDLE: /* External power connected in IDLE */
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
			led_set_color_battery_duty(LED_WHITE, 100);
		break;
	case LED_PWRS_FORCED_IDLE:
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
			led_set_color_battery_duty(
				(battery_ticks % LED_TICKS_PER_CYCLE <
				 LED_ON_TICKS) ?
					LED_AMBER :
					LED_OFF,
				(battery_ticks % LED_TICKS_PER_CYCLE <
				 LED_ON_TICKS) ?
					100 :
					0);
		}
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	if (skitty_led_pwr_get_state() != LED_PWRS_DISCHARGE) {
		battery_low_triggeied = 0;
		battery_critical_triggeied = 0;
		hook_call_deferred(&battery_set_pwm_led_tick_data, -1);
		return;
	}
}

static void battery_set_pwm_led_tick(void)
{
	uint32_t elapsed;
	uint32_t next = 0;
	uint32_t start = get_time().le.lo;

	if (batt_led_pulse.duty == 0) {
		batt_led_pulse.duty = 100;
		next = batt_led_pulse.on_time;
	} else if ((batt_led_pulse.duty - batt_led_pulse.duty_inc) < 0)
		batt_led_pulse.duty = 0;
	else
		batt_led_pulse.duty -= batt_led_pulse.duty_inc;

	led_set_color_battery_duty(batt_led_pulse.color, batt_led_pulse.duty);

	if (next == 0)
		next = batt_led_pulse.interval;
	elapsed = get_time().le.lo - start;
	next = next > elapsed ? next - elapsed : 0;
	hook_call_deferred(&battery_set_pwm_led_tick_data, next);
}

/* Called by hook task every TICK(IT83xx 500ms) */
static void battery_led_tick(void)
{
	led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, battery_led_tick, HOOK_PRIO_DEFAULT);
