/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for HX20
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "led_common.h"
#include "led_pwm.h"
#include "lid_switch.h"
#include "pwm.h"
#include "power_button.h"
#include "hooks.h"
#include "host_command.h"
#include "util.h"
#include "gpio.h"
#include "driver/temp_sensor/f75303.h"
#include "diagnostics.h"
#include "fan.h"
#include "host_command_customization.h"
#include "power.h"
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define LED_TICKS_PER_CYCLE 10
#define LED_ON_TICKS 5

/* at 8-bit mode one cycle = 8ms */
#define BREATH_ON_LENGTH	62
#define BREATH_OFF_LENGTH	200


const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_RIGHT_LED,
	EC_LED_ID_POWER_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

int power_button_enable = 0;

struct pwm_led led_color_map[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = {   8,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0,   8,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0,   8 },
	[EC_LED_COLOR_YELLOW] = {   4,   5,   0 },
	[EC_LED_COLOR_WHITE]  = {   4,  10,   5 },
	[EC_LED_COLOR_AMBER]  = {   9,   1,   0 },
};

struct pwm_led pwr_led_color_map[EC_LED_COLOR_COUNT] = {
				/* White, Green, Red */
	[EC_LED_COLOR_RED]    = {   0,   0,  15 },
	[EC_LED_COLOR_GREEN]  = {   0,  15,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0,   0 },
	[EC_LED_COLOR_YELLOW] = {   0,   5,  10 },
	[EC_LED_COLOR_WHITE]  = {  15,   0,   0 },
	[EC_LED_COLOR_AMBER]  = {   0,   5,  30 },
};

struct pwm_led breath_led_color_map[EC_LED_COLOR_COUNT] = {
				/* White, Green, Red */
	[EC_LED_COLOR_WHITE]  = {  50,   0,   0 },
};

struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	[PWM_LED0] = {
		/* left port LEDs */
		.ch0 = PWM_CH_DB0_LED_RED,
		.ch1 = PWM_CH_DB0_LED_GREEN,
		.ch2 = PWM_CH_DB0_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
	[PWM_LED1] = {
		/* right port LEDs */
		.ch0 = PWM_CH_DB1_LED_RED,
		.ch1 = PWM_CH_DB1_LED_GREEN,
		.ch2 = PWM_CH_DB1_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
	[PWM_LED2] = {
		/* Power Button LEDs */
		.ch0 = PWM_CH_FPR_LED_RED,
		.ch1 = PWM_CH_FPR_LED_GREEN,
		.ch2 = PWM_CH_FPR_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
};

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

void enable_pwr_breath(enum pwm_led_id id, int color, uint8_t enable)
{
	struct pwm_led duty = { 0 };
	const struct pwm_led *led = &pwm_leds[id];

	if ((id >= CONFIG_LED_PWM_COUNT) || (id < 0) ||
	    (color >= EC_LED_COLOR_COUNT) || (color < -1))
		return;

	if (color != -1) {
		duty.ch0 = breath_led_color_map[color].ch0;
		duty.ch1 = breath_led_color_map[color].ch1;
		duty.ch2 = breath_led_color_map[color].ch2;
	}

	if (led->ch0 != (enum pwm_channel)PWM_LED_NO_CHANNEL)
		bbled_enable(led->ch0, duty.ch0, BREATH_ON_LENGTH, BREATH_OFF_LENGTH, enable);
	if (led->ch1 != (enum pwm_channel)PWM_LED_NO_CHANNEL)
		bbled_enable(led->ch1, duty.ch1, BREATH_ON_LENGTH, BREATH_OFF_LENGTH, enable);
	if (led->ch2 != (enum pwm_channel)PWM_LED_NO_CHANNEL)
		bbled_enable(led->ch2, duty.ch2, BREATH_ON_LENGTH, BREATH_OFF_LENGTH, enable);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_YELLOW] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	enum pwm_led_id pwm_id;

	/* Convert ec_led_id to pwm_led_id. */
	if (led_id == EC_LED_ID_LEFT_LED)
		pwm_id = PWM_LED0;
	else if (led_id == EC_LED_ID_RIGHT_LED)
		pwm_id = PWM_LED1;
	else if (led_id == EC_LED_ID_POWER_LED)
		pwm_id = PWM_LED2;
	else
		return EC_ERROR_UNKNOWN;

	if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_RED])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_RED);
		else if (brightness[EC_LED_COLOR_GREEN])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_GREEN);
		else if (brightness[EC_LED_COLOR_BLUE])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_BLUE);
		else if (brightness[EC_LED_COLOR_YELLOW])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_YELLOW);
		else if (brightness[EC_LED_COLOR_WHITE])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER])
			set_pwr_led_color(pwm_id, EC_LED_COLOR_AMBER);
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

static void set_active_port_color(int color)
{
	int port_charging_active = 0;

	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)) {
		port_charging_active = gpio_get_level(GPIO_TYPEC2_VBUS_ON_EC) ||
								gpio_get_level(GPIO_TYPEC3_VBUS_ON_EC);
		set_pwm_led_color(PWM_LED0, port_charging_active ? color : -1);
	}

	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
		port_charging_active = gpio_get_level(GPIO_TYPEC0_VBUS_ON_EC) ||
								gpio_get_level(GPIO_TYPEC1_VBUS_ON_EC);
		set_pwm_led_color(PWM_LED1, port_charging_active ? color : -1);
	}
}

static void led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	if (power_button_batt_cutoff() && !gpio_get_level(GPIO_ON_OFF_BTN_L)) {
		set_pwm_led_color(PWM_LED0,
		(battery_ticks & 0x2) ? EC_LED_COLOR_RED : EC_LED_COLOR_BLUE);
		set_pwm_led_color(PWM_LED1,
		(battery_ticks & 0x2) ? EC_LED_COLOR_RED : EC_LED_COLOR_BLUE);
		return;
	}
	/* Blink both mainboard LEDS as a warning if the chasssis is open and power is on */
	if (!gpio_get_level(GPIO_CHASSIS_OPEN)) {
		set_pwm_led_color(PWM_LED0, (battery_ticks & 0x2) ? EC_LED_COLOR_RED : -1);
		set_pwm_led_color(PWM_LED1, (battery_ticks & 0x2) ? EC_LED_COLOR_RED : -1);
		return;
	}

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(EC_LED_COLOR_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			if (charge_get_percent() < 10)
				set_active_port_color((battery_ticks & 0x2) ?
					EC_LED_COLOR_RED : -1);
			else
				set_active_port_color(-1);
		}
		break;
	case PWR_STATE_ERROR:
		set_active_port_color((battery_ticks & 0x2) ?
				EC_LED_COLOR_WHITE : -1);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(EC_LED_COLOR_WHITE);
		break;
	case PWR_STATE_IDLE:
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			set_active_port_color((battery_ticks & 0x4) ?
					EC_LED_COLOR_AMBER : -1);
		else
			set_active_port_color(EC_LED_COLOR_WHITE);
		break;
	default:
		break;
	}

}

static void led_set_power(void)
{
	static int power_tick;

	power_tick++;

	/* don't light up when at lid close */
	if (!lid_is_open()) {
		set_pwr_led_color(PWM_LED2, -1);
		return;
	}

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		enable_pwr_breath(PWM_LED2, EC_LED_COLOR_WHITE, 1);
	else
		enable_pwr_breath(PWM_LED2, EC_LED_COLOR_WHITE, 0);

	if (chipset_in_state(CHIPSET_STATE_ON) | power_button_enable) {
		if (charge_prevent_power_on(0))
			set_pwr_led_color(PWM_LED2, (power_tick %
				LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
				EC_LED_COLOR_RED : -1);
		else
			set_pwr_led_color(PWM_LED2, EC_LED_COLOR_WHITE);
	} else
		set_pwr_led_color(PWM_LED2, -1);
}


uint32_t hw_diagnostics;
uint32_t hw_diagnostic_tick;
uint32_t hw_diagnostics_ctr;
uint8_t bios_section;
uint8_t bios_code;

uint8_t bios_complete;
uint8_t fan_seen;
uint8_t s0_seen;
uint8_t run_diagnostics;
void reset_diagnostics(void)
{
	hw_diagnostics =0;
	hw_diagnostics_ctr = 0;
	bios_complete = 0;
	bios_code = 0;
	bios_section = 0;
	hw_diagnostic_tick = 0;
	fan_seen = 0;
	s0_seen = 0;
	run_diagnostics = 1;
}

static void set_diagnostic_leds(int color)
{
	set_pwm_led_color(PWM_LED0, color);
	set_pwm_led_color(PWM_LED1, color);
}
#define TICK_PER_SEC	4
static bool diagnostics_tick(void)
{
	if (hw_diagnostics_ctr >= DIAGNOSTICS_MAX) {
		run_diagnostics = 0;
		return false;
	}
	if (run_diagnostics == 0) {
		return false;
	}

	if (bios_complete && ((bios_section & 0x80) == 0) && hw_diagnostics == 0) {
		/*exit boot condition - everything is ok after minimum 4 seconds of checking*/
		if (fan_seen){
			run_diagnostics = 0;
		}
		return false;
	}

	if (bios_section == TYPE_DDR && bios_code == TYPE_DDR_TRAINING_START) {
		set_diagnostic_leds(EC_LED_COLOR_GREEN);
		return true;
	}

	if (fan_get_rpm_actual(0) > 100) {
		fan_seen = true;
	}

	if (power_get_state() == POWER_S0) {
		s0_seen = true;
	}

	hw_diagnostic_tick++;

	if (hw_diagnostic_tick < 15*TICK_PER_SEC) {
		/*give us more time for checks to complete*/
		return false;
	}

	if (fan_seen == false) {
		set_hw_diagnostic(DIAGNOSTICS_NOFAN, true);
	}
	if (s0_seen == false) {
		set_hw_diagnostic(DIAGNOSTICS_NO_S0, true);
	}

	if (hw_diagnostic_tick & 0x01) {
		/*off*/

		set_diagnostic_leds(-1);
		return true;
	}

	if (hw_diagnostics_ctr == DIAGNOSTICS_START) {
		set_diagnostic_leds(EC_LED_COLOR_WHITE);
		CPRINTS("Boot issue: HW 0x%08x BIOS: 0x%04x", hw_diagnostics, bios_code);
	} else if (hw_diagnostics_ctr  < DIAGNOSTICS_HW_FINISH) {
		set_diagnostic_leds((hw_diagnostics & (1<<hw_diagnostics_ctr)) ? EC_LED_COLOR_RED : EC_LED_COLOR_GREEN);
	} else if (hw_diagnostics_ctr == DIAGNOSTICS_HW_FINISH) {
		set_diagnostic_leds(EC_LED_COLOR_AMBER);
	} else if (hw_diagnostics_ctr < DIAGNOSTICS_MAX) {
		set_diagnostic_leds((bios_code & (1<<(hw_diagnostics_ctr-DIAGNOSTICS_BIOS_BIT0)))
								? EC_LED_COLOR_BLUE : EC_LED_COLOR_GREEN);
	}



	hw_diagnostics_ctr++;
	return true;

}

static void diagnostic_check_tempsensor_deferred(void)
{
	int temps = 0;

	f75303_get_val(F75303_IDX_LOCAL, &temps);
	if (temps == 0)
			set_hw_diagnostic(DIAGNOSTICS_THERMAL_SENSOR, true);
}
DECLARE_DEFERRED(diagnostic_check_tempsensor_deferred);

static void diagnostics_check_devices(void)
{
	int device_id;

	gpio_set_flags(GPIO_TP_BOARD_ID, GPIO_PULL_DOWN);
	gpio_set_flags(GPIO_AD_BOARD_ID, GPIO_PULL_DOWN);
	usleep(10);
	gpio_set_flags(GPIO_TP_BOARD_ID, GPIO_FLAG_NONE);
	gpio_set_flags(GPIO_AD_BOARD_ID, GPIO_FLAG_NONE);
	usleep(2);
	device_id = get_hardware_id(ADC_TP_BOARD_ID);
	if (device_id <= BOARD_VERSION_3 ||  device_id >= BOARD_VERSION_14) {
		set_hw_diagnostic(DIAGNOSTICS_TOUCHPAD, true);
	}
	device_id = get_hardware_id(ADC_AUDIO_BOARD_ID);
	if (device_id <= BOARD_VERSION_3 ||  device_id >= BOARD_VERSION_14) {
		set_hw_diagnostic(DIAGNOSTICS_AUDIO_DAUGHTERBOARD, true);
	}

	hook_call_deferred(&diagnostic_check_tempsensor_deferred_data, 2000*MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME,
		diagnostics_check_devices,
		HOOK_PRIO_DEFAULT);

void set_hw_diagnostic(enum diagnostics_device_idx idx, bool error)
{
	if (error)
		hw_diagnostics |= 1 << idx;
}

void set_bios_diagnostic(uint8_t section, uint8_t code)
{
	bios_section = section;
	bios_code = code;

	if (section ==  TYPE_PORT80 && code == TYPE_PORT80_COMPLETE)
		bios_complete = true;

	if (section == TYPE_DDR && bios_code == TYPE_DDR_FAIL)
		set_hw_diagnostic(DIAGNOSTICS_NO_DDR, true);
}

/* Called by hook task every TICK */
static void led_tick(void)
{

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();

	if (diagnostics_tick()) {
		/* we have an error, override LED control*/
		return;
	}
	led_set_battery();
}

static void led_configure(void)
{
	int i;

	/*Initialize PWM channels*/
	for (i = 0; i < PWM_CH_COUNT; i++) {
		pwm_enable(i, 1);
	}
	led_tick();
}

DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
/*Run after PWM init is complete*/
DECLARE_HOOK(HOOK_INIT, led_configure, HOOK_PRIO_DEFAULT+1);

void power_button_enable_led(int enable)
{
	power_button_enable = enable;
}

