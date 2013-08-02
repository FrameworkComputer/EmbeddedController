/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_FMAP
#define CONFIG_POWER_BUTTON
#define CONFIG_TEMP_SENSOR
#undef CONFIG_WATCHDOG

/* Host test config */
#ifdef TEST_SMART_BATTERY_CHARGER
#define CONFIG_BATTERY_MOCK
#define CONFIG_CHARGER
#define CONFIG_CHARGER_INPUT_CURRENT 4032
#endif

/* Keyboard protocol */
#ifdef TEST_KB_8042
#define CONFIG_KEYBOARD_PROTOCOL_8042
#else
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#endif

/* Turbo-mode charger tests */
#ifdef TEST_EXTPOWER_FALCO
#define CONFIG_EXTPOWER_FALCO
#endif

#define CONFIG_WP_ACTIVE_HIGH

/* Module IDs */
enum module_id {
	MODULE_I2C,
	MODULE_UART,
};

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

enum adc_channel {
	ADC_CH_CHARGER_CURRENT,
	ADC_AC_ADAPTER_ID_VOLTAGE,

	ADC_CH_COUNT
};

#endif /* __BOARD_H */
