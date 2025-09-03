/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "led.h"


enum led_pwms_color_t {
	LED_PWMS_COLOR_RED,
	LED_PWMS_COLOR_GREEN,
	LED_PWMS_COLOR_BLUE,
	LED_PWMS_COUNT,
};

static void board_increase_led_duty(void)
{
	uint8_t white_color[LED_PWMS_COUNT];

	/* WIHTE */
	white_color[LED_PWMS_COLOR_RED] = 10;
	white_color[LED_PWMS_COLOR_GREEN] = 15;
	white_color[LED_PWMS_COLOR_BLUE] = 10;
	led_change_color(LED_WHITE, EC_LED_ID_BATTERY_LED, sizeof(white_color), white_color);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_increase_led_duty, HOOK_PRIO_DEFAULT);

static void board_decrease_led_duty(void)
{
	uint8_t white_color[LED_PWMS_COUNT];

	/* WIHTE */
	white_color[LED_PWMS_COLOR_RED] = 6;
	white_color[LED_PWMS_COLOR_GREEN] = 9;
	white_color[LED_PWMS_COLOR_BLUE] = 6;
	led_change_color(LED_WHITE, EC_LED_ID_BATTERY_LED, sizeof(white_color), white_color);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_decrease_led_duty, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_decrease_led_duty, HOOK_PRIO_DEFAULT);
