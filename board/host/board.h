/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#undef CONFIG_ACCEL_CALIBRATE
#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_FMAP
#define CONFIG_POWER_BUTTON
#undef CONFIG_WATCHDOG
#define CONFIG_SWITCH

#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 4

#define CONFIG_WP_ACTIVE_HIGH

enum gpio_signal {
	GPIO_EC_INT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
	GPIO_WP,
	GPIO_ENTERING_RW,
	GPIO_AC_PRESENT,
	GPIO_PCH_BKLTEN,
	GPIO_ENABLE_BACKLIGHT,
	GPIO_BUTTON_VOLUME_DOWN_L,
	GPIO_BUTTON_VOLUME_UP,

	GPIO_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_CPU = 0,
	TEMP_SENSOR_BOARD,
	TEMP_SENSOR_CASE,
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

enum adc_channel {
	ADC_CH_CHARGER_CURRENT,
	ADC_AC_ADAPTER_ID_VOLTAGE,

	ADC_CH_COUNT
};

/* Identifiers for each accelerometer used. */
enum accel_id {
	ACCEL_LID,
	ACCEL_BASE,

	/* Number of accelerometers. */
	ACCEL_COUNT
};

#endif /* __BOARD_H */
