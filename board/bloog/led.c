/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Bloog/Blooguard
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "led_common.h"
#include "hooks.h"

#define BAT_LED_ON 0
#define BAT_LED_OFF 1

#define POWER_LED_ON 0
#define POWER_LED_OFF 1

#define LED_TICKS_PER_CYCLE 10
#define LED_ON_TICKS 5

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_RIGHT_LED,
	EC_LED_ID_POWER_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static void led_set_color_battery(int port, enum led_color color)
{
	gpio_set_level(port ? GPIO_LED_AMBER_C1_L : GPIO_LED_AMBER_C0_L,
		(color == LED_AMBER) ? BAT_LED_ON : BAT_LED_OFF);
	gpio_set_level(port ? GPIO_LED_WHITE_C1_L : GPIO_LED_WHITE_C0_L,
		(color == LED_WHITE) ? BAT_LED_ON : BAT_LED_OFF);
}

void led_set_color_power(enum ec_led_colors color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_PWR_LED_WHITE_L, POWER_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_PWR_LED_WHITE_L, POWER_LED_ON);
		break;
	default:
		break;
	}
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
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		break;
	default:
		break;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(0, LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(0, LED_AMBER);
		else
			led_set_color_battery(0, LED_OFF);
		break;
	case EC_LED_ID_RIGHT_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(1, LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(1, LED_AMBER);
		else
			led_set_color_battery(1, LED_OFF);
		break;
	case EC_LED_ID_POWER_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_power(EC_LED_COLOR_WHITE);
		else
			led_set_color_power(LED_OFF);
		break;
	default:
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}

/*
 * Set active charge port color to the parameter, turn off all others.
 * If no port is active (-1), turn off all LEDs.
 */
static void set_active_port_color(enum led_color color)
{
	int port = charge_manager_get_active_charge_port();

	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
		led_set_color_battery(0, (port == 0) ? color : LED_OFF);
	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		led_set_color_battery(1, (port == 1) ? color : LED_OFF);
}

static void led_set_battery(void)
{
	static int battery_ticks;
	static int power_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	/*
	 * Override battery LEDs for Blooglet, Blooglet is non-power LED
	 * design, blinking both two side battery white LEDs to indicate
	 * system suspend with non-charging state.
	 */
	if (!board_is_convertible()) {
		if (chipset_in_state(CHIPSET_STATE_SUSPEND |
					 CHIPSET_STATE_STANDBY) &&
			charge_get_state() != PWR_STATE_CHARGE) {

			power_ticks++;

			led_set_color_battery(0, power_ticks & 0x4 ?
						  LED_WHITE : LED_OFF);
			led_set_color_battery(1, power_ticks & 0x4 ?
						  LED_WHITE : LED_OFF);
			return;
		}
	}

	power_ticks = 0;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			if (charge_get_percent() < 10)
				led_set_color_battery(1, (battery_ticks %
					LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
					LED_WHITE : LED_OFF);
			else
				led_set_color_battery(1, LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
			led_set_color_battery(0, LED_OFF);
		break;
	case PWR_STATE_ERROR:
		set_active_port_color((battery_ticks & 0x2) ?
				LED_WHITE : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			set_active_port_color((battery_ticks %
				LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
				LED_AMBER : LED_OFF);
		else
			set_active_port_color(LED_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

static void led_set_power(void)
{
	static int power_tick;

	power_tick++;

	if (chipset_in_state(CHIPSET_STATE_ON))
		led_set_color_power(LED_WHITE);
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND |
				  CHIPSET_STATE_STANDBY))
		led_set_color_power((power_tick %
			LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
			LED_WHITE : LED_OFF);
	else
		led_set_color_power(LED_OFF);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();

	led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
