/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_led.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "led_pwm.h"
#include "timer.h"
#include "util.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define BAT_LED_ON 0
#define BAT_LED_OFF 1

#define BATT_LOW_BCT 10

#define LED_TICKS_PER_CYCLE 4
#define LED_TICKS_PER_CYCLE_S3 4
#define LED_ON_TICKS 2
#define POWER_LED_ON_S3_TICKS 2

#define PWR_LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(324)

/*
 * Due to the CSME-Lite processing, upon startup the CPU transitions through
 * S0->S3->S5->S3->S0, causing the LED to turn on/off/on, so
 * delay turning off power LED during suspend/shutdown.
 */
#define PWR_LED_CPU_DELAY K_MSEC(2000)

static bool power_led_support;

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

static const struct board_led_pwm_dt_channel pwr_led =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(pwm_power_led));

static void pwr_led_pwm_set_duty(const struct board_led_pwm_dt_channel *ch,
				 int percent)
{
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(ch->dev)) {
		LOG_ERR("device ");
		// LOG_ERR("device %s not ready", ch->dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(PWR_LED_PWM_PERIOD_NS * percent, 100);

	LOG_DBG("PWM LED %s set percent (%d), pulse %d", ch->dev->name, percent,
		pulse_ns);

	rv = pwm_set(ch->dev, ch->channel, PWR_LED_PWM_PERIOD_NS, pulse_ns,
		     ch->flags);
	if (rv) {
		LOG_ERR("pwm_set() failed %s (%d)", ch->dev->name, rv);
	}
}

static void led_set_color_battery(enum led_color color)
{
	const struct gpio_dt_spec *amber_led, *white_led;

	amber_led = GPIO_DT_FROM_NODELABEL(gpio_battery_led_amber_l);
	white_led = GPIO_DT_FROM_NODELABEL(gpio_battery_led_white_l);

	switch (color) {
	case LED_WHITE:
		gpio_pin_set_dt(white_led, BAT_LED_ON);
		gpio_pin_set_dt(amber_led, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_pin_set_dt(white_led, BAT_LED_OFF);
		gpio_pin_set_dt(amber_led, BAT_LED_ON);
		break;
	case LED_OFF:
		gpio_pin_set_dt(white_led, BAT_LED_OFF);
		gpio_pin_set_dt(amber_led, BAT_LED_OFF);
		break;
	default:
		break;
	}
}

static int led_set_color_power(enum led_color color, int duty)
{
	/* PWM LED duty range from 0% ~ 100% */
	if (duty < 0 || 100 < duty)
		return EC_ERROR_UNKNOWN;

	switch (color) {
	case LED_OFF:
		pwr_led_pwm_set_duty(&pwr_led, 0);
		break;
	case LED_WHITE:
		pwr_led_pwm_set_duty(&pwr_led, duty);
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
	case EC_LED_ID_POWER_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 100;
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
			led_set_color_battery(LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LED_AMBER);
		else
			led_set_color_battery(LED_OFF);
		break;
	case EC_LED_ID_POWER_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			rv = led_set_color_power(
				LED_WHITE, brightness[EC_LED_COLOR_WHITE]);
		else
			rv = led_set_color_power(LED_OFF, 0);
		break;
	default:
		rv = EC_ERROR_PARAM1;
	}

	return rv;
}

static void led_set_battery(void)
{
	static unsigned int battery_ticks;
	static int suspend_ticks;

	battery_ticks++;

	if (!power_led_support && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    led_pwr_get_state() != LED_PWRS_CHARGE) {
		suspend_ticks++;

		led_set_color_battery(suspend_ticks % LED_TICKS_PER_CYCLE_S3 <
						      POWER_LED_ON_S3_TICKS ?
					      LED_WHITE :
					      LED_OFF);
		return;
	}

	suspend_ticks = 0;

	switch (led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		/* Always indicate when charging, even in suspend. */
		led_set_color_battery(LED_AMBER);
		break;
	case LED_PWRS_DISCHARGE:
		if (charge_get_percent() < BATT_LOW_BCT)
			led_set_color_battery(
				(battery_ticks % LED_TICKS_PER_CYCLE <
				 LED_ON_TICKS) ?
					LED_WHITE :
					LED_OFF);
		else
			led_set_color_battery(LED_OFF);
		break;
	case LED_PWRS_ERROR:
		led_set_color_battery((battery_ticks & 0x1) ? LED_AMBER :
							      LED_OFF);
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		led_set_color_battery(LED_WHITE);
		break;
	case LED_PWRS_IDLE: /* External power connected in IDLE */
		led_set_color_battery(LED_WHITE);
		break;
	case LED_PWRS_FORCED_IDLE:
		led_set_color_battery(
			(battery_ticks % LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
				LED_AMBER :
				LED_OFF);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

static void power_led_check(void)
{
	int ret;
	uint32_t val;

	/*
	 * Retrieve the tablet config.
	 */
	ret = cros_cbi_get_fw_config(FW_FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_FORM_FACTOR);
		return;
	}

	if (val == FW_FORM_FACTOR_CONVERTIBLE)
		power_led_support = true;
	else /* Clameshell */
		power_led_support = false;
}
DECLARE_HOOK(HOOK_INIT, power_led_check, HOOK_PRIO_DEFAULT);

/* Called by hook task every TICK(IT83xx 500ms) */
static void battery_led_tick(void)
{
	led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, battery_led_tick, HOOK_PRIO_DEFAULT);

#define PWR_LED_PULSE_US (1500 * MSEC)
#define PWR_LED_OFF_TIME_US (1500 * MSEC)
/* 30 msec for nice and smooth transition. */
#define PWR_LED_PULSE_TICK_US (30 * MSEC)

enum power_led_mode {
	MODE_NO_CHANGE = 0,
	MODE_NORMAL = 1,
	MODE_SUSPEND = 2,
	MODE_OFF = 3,
};

static void pwr_led_tick(void);
DECLARE_DEFERRED(pwr_led_tick);

/*
 * When pulsing is enabled, brightness is incremented by <duty_inc> every
 * <interval> usec from 0 to 100% in LED_PULSE_US usec. Then it's decremented
 * likewise in PWR_LED_PULSE_US usec. Stay 0 for <off_time>.
 */
static struct {
	uint32_t interval;
	int duty_inc;
	enum led_color color;
	uint32_t off_time;
	int duty;
} pwr_led_pulse;

static atomic_t next_mode;

static void pwr_led_change_mode(enum power_led_mode mode)
{
	atomic_set(&next_mode, mode);
	hook_call_deferred(&pwr_led_tick_data, 0);
}

#define PWR_LED_CONFIG_TICK(interval, color)                                   \
	pwr_led_config_tick((interval), 100 / (PWR_LED_PULSE_US / (interval)), \
			    (color), (PWR_LED_OFF_TIME_US))

static void pwr_led_config_tick(uint32_t interval, int duty_inc,
				enum led_color color, uint32_t off_time)
{
	pwr_led_pulse.interval = interval;
	pwr_led_pulse.duty_inc = duty_inc;
	pwr_led_pulse.color = color;
	pwr_led_pulse.off_time = off_time;
	pwr_led_pulse.duty = 0;
}

static void pwr_led_tick(void)
{
	static enum power_led_mode current_mode;
	enum power_led_mode new_mode = atomic_clear(&next_mode);

	if (new_mode != MODE_NO_CHANGE && new_mode != current_mode) {
		current_mode = new_mode;
		switch (current_mode) {
		case MODE_NO_CHANGE:
			break; /* LCOV_EXCL_LINE excluded by above condition */
		case MODE_NORMAL:
			if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
				led_set_color_power(LED_WHITE, 100);
			break;
		case MODE_SUSPEND:
			PWR_LED_CONFIG_TICK(PWR_LED_PULSE_TICK_US, LED_WHITE);
			break;
		case MODE_OFF:
			if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
				led_set_color_power(LED_OFF, 0);
			break;
		}
	}

	if (current_mode != MODE_SUSPEND) {
		/* Other modes are constant on or off. */
		return;
	}

	uint32_t elapsed;
	uint32_t next = 0;
	uint32_t start = get_time().le.lo;

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED)) {
		led_set_color_power(pwr_led_pulse.color, pwr_led_pulse.duty);
		if (pwr_led_pulse.duty + pwr_led_pulse.duty_inc > 100) {
			pwr_led_pulse.duty_inc *= -1;
		} else if (pwr_led_pulse.duty + pwr_led_pulse.duty_inc < 0) {
			pwr_led_pulse.duty_inc *= -1;
			next = pwr_led_pulse.off_time;
		}
		pwr_led_pulse.duty += pwr_led_pulse.duty_inc;
	}

	if (next == 0)
		next = pwr_led_pulse.interval;
	elapsed = get_time().le.lo - start;
	next = next > elapsed ? next - elapsed : 0;
	hook_call_deferred(&pwr_led_tick_data, next);
}

/*
 * Timer for handling delays on suspend and shutdown. This needs
 * to be cancellable from non-workqueue threads, so it uses a timer
 * rather than deferred work because deferred work may be impossible
 * to cancel if currently running because it was preempted.
 */
K_TIMER_DEFINE(shutdown_timer, NULL, NULL);

static void pwr_led_suspend(struct k_timer *unused_timer)
{
	pwr_led_change_mode(MODE_SUSPEND);
}

static void pwr_led_shutdown(struct k_timer *unused_timer)
{
	pwr_led_change_mode(MODE_OFF);
}

static void pwr_led_shutdown_hook(void)
{
	k_timer_stop(&shutdown_timer);
	k_timer_init(&shutdown_timer, pwr_led_shutdown, NULL);
	k_timer_start(&shutdown_timer, PWR_LED_CPU_DELAY, K_FOREVER);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pwr_led_shutdown_hook, HOOK_PRIO_DEFAULT);

static void pwr_led_suspend_hook(void)
{
	k_timer_stop(&shutdown_timer);
	k_timer_init(&shutdown_timer, pwr_led_suspend, NULL);
	k_timer_start(&shutdown_timer, PWR_LED_CPU_DELAY, K_FOREVER);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwr_led_suspend_hook, HOOK_PRIO_DEFAULT);

static void pwr_led_resume(void)
{
	/*
	 * Avoid invoking the suspend/shutdown delayed hooks.
	 */
	k_timer_stop(&shutdown_timer);

	pwr_led_change_mode(MODE_NORMAL);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pwr_led_resume, HOOK_PRIO_DEFAULT);

/*
 * Since power led is controlled by functions called only when power state
 * change, we need to make sure that power led is in right state when EC
 * init, especially for sysjump case.
 */
static void pwr_led_init(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		pwr_led_resume();
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		pwr_led_suspend_hook();
	else
		pwr_led_shutdown_hook();
}
DECLARE_HOOK(HOOK_INIT, pwr_led_init, HOOK_PRIO_DEFAULT);

/*
 * Since power led is controlled by functions called only when power state
 * change, we need to restore it to previous state when led auto control
 * is enabled.
 */
void board_led_auto_control(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		led_set_color_power(LED_WHITE, 100);
		pwr_led_resume();
	} else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		pwr_led_suspend_hook();
	else {
		led_set_color_power(LED_OFF, 0);
		pwr_led_shutdown_hook();
	}
}
