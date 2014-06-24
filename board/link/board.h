/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Link mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_AP_HANG_DETECT
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BACKLIGHT_REQ_GPIO GPIO_PCH_BKLTEN
#define CONFIG_BATTERY_LINK
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_OVERRIDE_PARAMS
#define CONFIG_BOARD_VERSION
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V1
#define CONFIG_CHARGER_BQ24725
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_IVYBRIDGE
#define CONFIG_POWER_COMMON
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FANS 1
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_DRIVER_DS2413
#define CONFIG_ONEWIRE
#define CONFIG_PECI_TJMAX 105
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_SWITCH_DEDICATED_RECOVERY
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_PGOOD_1_8VS
#define CONFIG_TEMP_SENSOR_TMP006
#define CONFIG_UART_HOST 1
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_VBOOT_HASH
#define CONFIG_WIRELESS
#define CONFIG_WP_ACTIVE_HIGH

#ifndef __ASSEMBLER__

enum adc_channel {
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,
	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,

	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* Input sensor resistor, mOhm */
#define CONFIG_CHARGER_INPUT_CURRENT 4032 /* mA, based on Link HW design */
#define CONFIG_CHARGER_CURRENT_LIMIT 3000 /* PL102 inductor 3.0A(3.8A) */

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0  /* Note: proto0 used port 1 */
#define I2C_PORT_THERMAL 5
#define I2C_PORT_LIGHTBAR 1
#define I2C_PORT_REGULATOR 0

/* 13x8 keyboard scanner uses an entire GPIO bank for row inputs */
#define KB_SCAN_ROW_IRQ  LM4_IRQ_GPION
#define KB_SCAN_ROW_GPIO LM4_GPIO_N

/* Host connects to keyboard controller module via LPC */
#define HOST_KB_BUS_LPC

#include "gpio_signal.h"

/* x86 signal definitions */
enum x86_signal {
	X86_PGOOD_5VALW = 0,
	X86_PGOOD_1_5V_DDR,
	X86_PGOOD_1_5V_PCH,
	X86_PGOOD_1_8VS,
	X86_PGOOD_VCCP,
	X86_PGOOD_VCCSA,
	X86_PGOOD_CPU_CORE,
	X86_PGOOD_VGFX_CORE,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_S5_DEASSERTED,
	X86_SLP_A_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_SLP_ME_DEASSERTED,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	/* TMP006 U20, die/object temperature near Mini-DP / USB connectors */
	TEMP_SENSOR_I2C_U20_DIE = 0,
	TEMP_SENSOR_I2C_U20_OBJECT,
	/* TMP006 U11, die/object temperature near PCH */
	TEMP_SENSOR_I2C_U11_DIE,
	TEMP_SENSOR_I2C_U11_OBJECT,
	/* TMP006 U27, die/object temperature near hinge */
	TEMP_SENSOR_I2C_U27_DIE,
	TEMP_SENSOR_I2C_U27_OBJECT,
	/* TMP006 U14, die/object temperature near battery charger */
	TEMP_SENSOR_I2C_U14_DIE,
	TEMP_SENSOR_I2C_U14_OBJECT,
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL,
	/* CPU die temperature via PECI */
	TEMP_SENSOR_CPU_PECI,

	TEMP_SENSOR_COUNT
};

/* The number of TMP006 sensor chips on the board. */
#define TMP006_COUNT 4

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_RADIO_ENABLE_WLAN
#define WIRELESS_GPIO_BLUETOOTH GPIO_RADIO_ENABLE_BT
#define WIRELESS_GPIO_WLAN_POWER GPIO_ENABLE_WLAN

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
