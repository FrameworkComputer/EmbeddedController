/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Auron mainboard */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BACKLIGHT_REQ_GPIO GPIO_PCH_BKLTEN
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V1
#define CONFIG_CHARGER_BQ24707A
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_DISCHARGE_ON_AC_CUSTOM
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_HASWELL
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_CMD_GSV
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FANS 1
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_PECI_TJMAX 100
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_SWITCH_DEDICATED_RECOVERY
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_G781
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_PP3300_DX_EN
#define CONFIG_THROTTLE_AP
#define CONFIG_UART_HOST 2
#define CONFIG_USB_PORT_POWER_DUMB
#define CONFIG_VBOOT_HASH
#define CONFIG_WIRELESS

#ifndef __ASSEMBLER__

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0
#define I2C_PORT_THERMAL 5

/* 13x8 keyboard scanner uses an entire GPIO bank for row inputs */
#define KB_SCAN_ROW_IRQ  LM4_IRQ_GPIOK
#define KB_SCAN_ROW_GPIO LM4_GPIO_K

/* Host connects to keyboard controller module via LPC */
#define HOST_KB_BUS_LPC

/* USB ports */
#define USB_PORT_COUNT 2

#include "gpio_signal.h"

/* power signal definitions */
enum power_signal {
	X86_PGOOD_PP5000 = 0,
	X86_PGOOD_PP1350,
	X86_PGOOD_PP1050,
	X86_PGOOD_VCORE,
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S5_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* Input sensor resistor, mOhm */
#define CONFIG_CHARGER_INPUT_CURRENT 3078 /* mA, 90% of power supply rating */

enum adc_channel {
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,

	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

enum temp_sensor_id {
	/* CPU die temperature via PECI */
	TEMP_SENSOR_CPU_PECI = 0,
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL,
	/* G781 internal and external sensors */
	TEMP_SENSOR_I2C_G781_INTERNAL,
	TEMP_SENSOR_I2C_G781_EXTERNAL,

	TEMP_SENSOR_COUNT
};

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WWAN GPIO_PP3300_LTE_EN
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
