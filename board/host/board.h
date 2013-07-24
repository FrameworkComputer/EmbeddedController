/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HOSTCMD
#define CONFIG_HOST_EMU
#define CONFIG_LID_SWITCH
#define CONFIG_POWER_BUTTON
#define CONFIG_TEMP_SENSOR
#undef CONFIG_WATCHDOG

/* Host test config */
#ifdef SMART_BATTERY_CHARGER
#define CONFIG_CHARGER
#define CONFIG_MOCK_BATTERY
#define CONFIG_CHARGER_INPUT_CURRENT 4032
#endif

/* Keyboard protocol */
#ifdef KB_8042
#define CONFIG_KEYBOARD_PROTOCOL_8042
#else
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#endif

#define CONFIG_WP_ACTIVE_HIGH

enum gpio_signal {
	GPIO_EC_INT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
	GPIO_WP,
	GPIO_ENTERING_RW,
	GPIO_AC_PRESENT,

	GPIO_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_CPU = 0,
	TEMP_SENSOR_BOARD,
	TEMP_SENSOR_CASE,

	TEMP_SENSOR_COUNT
};

#endif /* __BOARD_H */
