/* Copyright 2022 The ChromiumOS Authors
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

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

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
#define PWR_LED_CPU_DELAY_MS (2000 * MSEC)

static bool power_led_support;

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_RIGHT_LED,
					     EC_LED_ID_LEFT_LED,
					     EC_LED_ID_POWER_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

enum led_port { RIGHT_PORT = 0, LEFT_PORT };

static const struct board_led_pwm_dt_channel pwr_led =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(pwm_power_led));

static void pwr_led_pwm_set_duty(const struct board_led_pwm_dt_channel *ch,
				 int percent)
{
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(ch->dev)) {
		LOG_ERR("PWM device %s not ready", ch->dev->name);
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

static void led_set_color_battery(int port, enum led_color color)
{
	const struct gpio_dt_spec *amber_led, *white_led;

	if (port == RIGHT_PORT) {
		amber_led = GPIO_DT_FROM_NODELABEL(gpio_c0_charger_led_amber_l);
		white_led = GPIO_DT_FROM_NODELABEL(gpio_c0_charger_led_white_l);
	} else if (port == LEFT_PORT) {
		amber_led = GPIO_DT_FROM_NODELABEL(gpio_c1_charger_led_amber_l);
		white_led = GPIO_DT_FROM_NODELABEL(gpio_c1_charger_led_white_l);
	}

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
	case EC_LED_ID_LEFT_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		break;
	case EC_LED_ID_RIGHT_LED:
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
	int rv;

	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(LEFT_PORT, LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LEFT_PORT, LED_AMBER);
		else
			led_set_color_battery(LEFT_PORT, LED_OFF);
		break;
	case EC_LED_ID_RIGHT_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(RIGHT_PORT, LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(RIGHT_PORT, LED_AMBER);
		else
			led_set_color_battery(RIGHT_PORT, LED_OFF);
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

	if (rv)
		return rv;

	return EC_SUCCESS;
}

/*
 * Set active charge port color to the parameter, turn off all others.
 * If no port is active (-1), turn off all LEDs.
 */
static void set_active_port_color(enum led_color color)
{
	int port = charge_manager_get_active_charge_port();

	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		led_set_color_battery(RIGHT_PORT,
				      (port == RIGHT_PORT) ? color : LED_OFF);
	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
		led_set_color_battery(LEFT_PORT,
				      (port == LEFT_PORT) ? color : LED_OFF);
}

static void led_set_battery(void)
{
	static unsigned int battery_ticks;
	static int suspend_ticks;

	battery_ticks++;

	/*
	 * Override battery LEDs for Yavilla without power led support,
	 * blinking both two side battery white LEDs to indicate
	 * system suspend with non-charging state.
	 */
	if (!power_led_support && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    led_pwr_get_state() != LED_PWRS_CHARGE) {
		suspend_ticks++;

		led_set_color_battery(RIGHT_PORT,
				      suspend_ticks % LED_TICKS_PER_CYCLE_S3 <
						      POWER_LED_ON_S3_TICKS ?
					      LED_WHITE :
					      LED_OFF);
		led_set_color_battery(LEFT_PORT,
				      suspend_ticks % LED_TICKS_PER_CYCLE_S3 <
						      POWER_LED_ON_S3_TICKS ?
					      LED_WHITE :
					      LED_OFF);
		return;
	}

	suspend_ticks = 0;

	switch (led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case LED_PWRS_DISCHARGE:
		/*
		 * Blinking amber LEDs slowly if battery is lower 10
		 * percentage.
		 */
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			if (charge_get_percent() < BATT_LOW_BCT)
				led_set_color_battery(
					RIGHT_PORT,
					(battery_ticks % LED_TICKS_PER_CYCLE <
					 LED_ON_TICKS) ?
						LED_AMBER :
						LED_OFF);
			else
				led_set_color_battery(RIGHT_PORT, LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)) {
			if (charge_get_percent() < BATT_LOW_BCT)
				led_set_color_battery(
					LEFT_PORT,
					(battery_ticks % LED_TICKS_PER_CYCLE <
					 LED_ON_TICKS) ?
						LED_AMBER :
						LED_OFF);
			else
				led_set_color_battery(LEFT_PORT, LED_OFF);
		}
		break;
	case LED_PWRS_ERROR:
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			led_set_color_battery(
				RIGHT_PORT,
				(battery_ticks & 0x1) ? LED_AMBER : LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)) {
			led_set_color_battery(LEFT_PORT, (battery_ticks & 0x1) ?
								 LED_AMBER :
								 LED_OFF);
		}
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case LED_PWRS_IDLE: /* External power connected in IDLE */
		set_active_port_color(LED_WHITE);
		break;
	case LED_PWRS_FORCED_IDLE:
		set_active_port_color(
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
	ret = cros_cbi_get_fw_config(FW_TABLET, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TABLET);
		return;
	}

	if (val == FW_TABLET_PRESENT)
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

static void pwr_led_tick(void);
DECLARE_DEFERRED(pwr_led_tick);
static void pwr_led_tick(void)
{
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

static void pwr_led_suspend(void)
{
	PWR_LED_CONFIG_TICK(PWR_LED_PULSE_TICK_US, LED_WHITE);
	pwr_led_tick();
}
DECLARE_DEFERRED(pwr_led_suspend);

static void pwr_led_shutdown(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_color_power(LED_OFF, 0);
}
DECLARE_DEFERRED(pwr_led_shutdown);

static void pwr_led_shutdown_hook(void)
{
	hook_call_deferred(&pwr_led_tick_data, -1);
	hook_call_deferred(&pwr_led_suspend_data, -1);
	hook_call_deferred(&pwr_led_shutdown_data, PWR_LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pwr_led_shutdown_hook, HOOK_PRIO_DEFAULT);

static void pwr_led_suspend_hook(void)
{
	hook_call_deferred(&pwr_led_shutdown_data, -1);
	hook_call_deferred(&pwr_led_suspend_data, PWR_LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwr_led_suspend_hook, HOOK_PRIO_DEFAULT);

static void pwr_led_resume(void)
{
	/*
	 * Assume there is no race condition with pwr_led_tick, which also
	 * runs in hook_task.
	 */
	hook_call_deferred(&pwr_led_tick_data, -1);
	/*
	 * Avoid invoking the suspend/shutdown delayed hooks.
	 */
	hook_call_deferred(&pwr_led_suspend_data, -1);
	hook_call_deferred(&pwr_led_shutdown_data, -1);
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_color_power(LED_WHITE, 100);
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
	if (chipset_in_state(CHIPSET_STATE_ON))
		pwr_led_resume();
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		pwr_led_suspend_hook();
	else
		pwr_led_shutdown_hook();
}
