/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */

#include "battery.h"
#include "hooks.h"
#include "led_common.h"

#include <zephyr/drivers/gpio.h>

#define SIDE_LED_ON 1
#define SIDE_LED_OFF 0

#define C0_CHG_LED GPIO_DT_FROM_NODELABEL(gpio_ec_led_c0_charging)
#define C0_FULL_LED GPIO_DT_FROM_NODELABEL(gpio_ec_led_c0_full_chg)
#define C1_CHG_LED GPIO_DT_FROM_NODELABEL(gpio_ec_led_c1_charging)
#define C1_FULL_LED GPIO_DT_FROM_NODELABEL(gpio_ec_led_c1_full_chg)

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_RIGHT_LED,
	EC_LED_ID_LEFT_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color { LED_OFF = 0, LED_AMBER, LED_WHITE, LED_COLOR_COUNT };

static void side_led_set_color(int port, enum led_color color)
{
	gpio_pin_set_dt(port ? C1_CHG_LED : C0_CHG_LED,
			(color == LED_AMBER) ? SIDE_LED_ON : SIDE_LED_OFF);
	gpio_pin_set_dt(port ? C1_FULL_LED : C0_FULL_LED,
			(color == LED_WHITE) ? SIDE_LED_ON : SIDE_LED_OFF);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_AMBER] = 1;
	brightness_range[EC_LED_COLOR_WHITE] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	int port;

	switch (led_id) {
	case EC_LED_ID_RIGHT_LED:
		port = 0;
		break;
	case EC_LED_ID_LEFT_LED:
		port = 1;
		break;
	default:
		return EC_ERROR_PARAM1;
	}

	if (brightness[EC_LED_COLOR_WHITE] != 0)
		side_led_set_color(port, LED_WHITE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		side_led_set_color(port, LED_AMBER);
	else
		side_led_set_color(port, LED_OFF);

	return EC_SUCCESS;
}

static void set_active_port_color(int port, enum led_color color)
{
	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED) && port == 0)
		side_led_set_color(0, color);
	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED) && port == 1)
		side_led_set_color(1, color);
}

static void board_led_update(void)
{
	/*
	 * TODO: b/454187289, Battery cannot be accessed when the AP is on.
	 * We need get battery status from ADSP but the  ADSP firmware is no
	 * available currently, so set the full_charged to false at present.
	 */
	bool full_charged = false;

	int C0_LED_PG = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl));
	int C1_LED_PG = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl));

	if (C0_LED_PG == 1) {
		set_active_port_color(0, full_charged ? LED_WHITE : LED_AMBER);
	} else {
		set_active_port_color(0, LED_OFF);
	}
	if (C1_LED_PG == 1) {
		set_active_port_color(1, full_charged ? LED_WHITE : LED_AMBER);
	} else {
		set_active_port_color(1, LED_OFF);
	}
}
DECLARE_HOOK(HOOK_SECOND, board_led_update, HOOK_PRIO_DEFAULT);
