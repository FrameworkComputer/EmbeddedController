/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dirks specific LED settings. */

#include "board_led.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#define LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(2000)

/*
 * Due to the CSME-Lite processing, upon startup the CPU transitions through
 * S0->S3->S5->S3->S0, causing the LED to turn on/off/on, so
 * delay turning off the LED during suspend/shutdown.
 */
#define LED_CPU_DELAY_MS (2000 * USEC_PER_MSEC)

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_POWER_LED };
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_WHITE,
	LED_RED,
	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

static const struct board_led_pwm_dt_channel pwr_amber_led =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(pwm_pwr_red_led));

static const struct board_led_pwm_dt_channel pwr_white_led =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(pwm_pwr_white_led));

static void led_pwm_set_duty(const struct board_led_pwm_dt_channel *ch,
			     int percent)
{
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(ch->dev)) {
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(LED_PWM_PERIOD_NS * percent, 100);

	rv = pwm_set(ch->dev, ch->channel, LED_PWM_PERIOD_NS, pulse_ns,
		     ch->flags);
}

test_mockable_static int set_color_power(enum led_color color, int duty)
{
	int white = 0;
	int red = 0;

	if (duty < 0 || 100 < duty)
		return EC_ERROR_UNKNOWN;

	switch (color) {
	case LED_OFF:
		break;
	case LED_WHITE:
		white = 1;
		break;
	case LED_RED:
		red = 1;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	if (red)
		led_pwm_set_duty(&pwr_amber_led, duty);
	else
		led_pwm_set_duty(&pwr_amber_led, 0);

	if (white)
		led_pwm_set_duty(&pwr_white_led, duty);
	else
		led_pwm_set_duty(&pwr_white_led, 0);

	return EC_SUCCESS;
}

static int set_color(enum ec_led_id id, enum led_color color, int duty)
{
	switch (id) {
	case EC_LED_ID_POWER_LED:
		return set_color_power(color, duty);
	default:
		return EC_ERROR_UNKNOWN;
	}
}

#define LED_PULSE_US (2 * USEC_PER_SEC)
/* 40 msec for nice and smooth transition. */
#define LED_PULSE_TICK_US (40 * USEC_PER_MSEC)

/*
 * When pulsing is enabled, brightness is incremented by <duty_inc> every
 * <interval> usec from 0 to 100% in LED_PULSE_US usec. Then it's decremented
 * likewise in LED_PULSE_US usec.
 */
static struct {
	uint32_t interval;
	int duty_inc;
	enum led_color color;
	int duty;
} led_pulse;

#define CONFIG_TICK(interval, color) \
	config_tick((interval), 100 / (LED_PULSE_US / (interval)), (color))

static void config_tick(uint32_t interval, int duty_inc, enum led_color color)
{
	led_pulse.interval = interval;
	led_pulse.duty_inc = duty_inc;
	led_pulse.color = color;
	led_pulse.duty = 0;
}

static void pulse_power_led(enum led_color color)
{
	set_color(EC_LED_ID_POWER_LED, color, led_pulse.duty);
	if (led_pulse.duty + led_pulse.duty_inc > 100)
		led_pulse.duty_inc = led_pulse.duty_inc * -1;
	else if (led_pulse.duty + led_pulse.duty_inc < 0)
		led_pulse.duty_inc = led_pulse.duty_inc * -1;
	led_pulse.duty += led_pulse.duty_inc;
}

static void led_tick(void);
DECLARE_DEFERRED(led_tick);
static void led_tick(void)
{
	uint32_t elapsed;
	uint32_t next = 0;
	uint32_t start = get_time().le.lo;

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		pulse_power_led(led_pulse.color);
	elapsed = get_time().le.lo - start;
	next = led_pulse.interval > elapsed ? led_pulse.interval - elapsed : 0;
	hook_call_deferred(&led_tick_data, next);
}

static void led_suspend(void)
{
	CONFIG_TICK(LED_PULSE_TICK_US, LED_WHITE);
	led_tick();
}
DECLARE_DEFERRED(led_suspend);

static void led_shutdown(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_OFF, 0);
}
DECLARE_DEFERRED(led_shutdown);

static void led_shutdown_hook(void)
{
	hook_call_deferred(&led_tick_data, -1);
	hook_call_deferred(&led_suspend_data, -1);
	hook_call_deferred(&led_shutdown_data, LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, led_shutdown_hook, HOOK_PRIO_DEFAULT);

static void led_suspend_hook(void)
{
	hook_call_deferred(&led_shutdown_data, -1);
	hook_call_deferred(&led_suspend_data, LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, led_suspend_hook, HOOK_PRIO_DEFAULT);

static void led_resume(void)
{
	/*
	 * Assume there is no race condition with led_tick, which also
	 * runs in hook_task.
	 */
	hook_call_deferred(&led_tick_data, -1);
	/*
	 * Avoid invoking the suspend/shutdown delayed hooks.
	 */
	hook_call_deferred(&led_suspend_data, -1);
	hook_call_deferred(&led_shutdown_data, -1);
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_WHITE, 100);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, led_resume, HOOK_PRIO_DEFAULT);

static void led_init(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		set_color(EC_LED_ID_POWER_LED, LED_WHITE, 100);
	else
		set_color(EC_LED_ID_POWER_LED, LED_WHITE, 0);
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_POST_PWM);

void board_led_auto_control(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		led_resume();
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		led_suspend_hook();
	else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		led_shutdown_hook();
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, '\0',
	       sizeof(*brightness_range) * EC_LED_COLOR_COUNT);

	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
}

int led_set_brightness(enum ec_led_id id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_WHITE])
		return set_color(id, LED_WHITE, brightness[EC_LED_COLOR_WHITE]);
	else if (brightness[EC_LED_COLOR_RED])
		return set_color(id, LED_RED, brightness[EC_LED_COLOR_RED]);
	else
		return set_color(id, LED_OFF, 0);
}
