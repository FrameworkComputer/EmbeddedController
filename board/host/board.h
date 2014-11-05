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
#undef CONFIG_WATCHDOG
#define CONFIG_SWITCH
#define CONFIG_INDUCTIVE_CHARGING

#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 4

#define CONFIG_WP_ACTIVE_HIGH

#include "gpio_signal.h"

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

/* Charge suppliers */
enum charge_supplier {
	CHARGE_SUPPLIER_TEST1,
	CHARGE_SUPPLIER_TEST2,
	CHARGE_SUPPLIER_TEST3,
	CHARGE_SUPPLIER_TEST4,
	CHARGE_SUPPLIER_TEST5,
	CHARGE_SUPPLIER_TEST6,
	CHARGE_SUPPLIER_TEST7,
	CHARGE_SUPPLIER_TEST8,
	CHARGE_SUPPLIER_TEST9,
	CHARGE_SUPPLIER_COUNT
};

/* supplier_priority table defined in board.c */
extern const int supplier_priority[];

/* Set the active charge port. */
void board_set_active_charge_port(int charge_port);

/* Set the charge current limit. */
void board_set_charge_limit(int charge_ma);

#endif /* __BOARD_H */
