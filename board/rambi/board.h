/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Rambi mainboard */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_AP_HANG_DETECT
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V1
#define CONFIG_CHARGER_BQ24715
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 1700   /* 33 W adapter, 19 V, 1.75 A */
#define CONFIG_CHARGER_SENSE_RESISTOR 10    /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* Input senso resistor, mOhm */
#define CONFIG_CHIPSET_BAYTRAIL
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_CMD_GSV
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_IRQ_GPIO GPIO_KBD_IRQ_L
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#undef  CONFIG_PECI
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_PWM
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432
#define CONFIG_USB_PORT_POWER_IN_S3
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_SIMPLE
#define CONFIG_VBOOT_HASH
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)

#undef DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 8

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

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WWAN GPIO_PP3300_LTE_EN
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN

#include "gpio_signal.h"

/* power signal definitions */
enum power_signal {
	X86_PGOOD_PP1050 = 0,
	X86_PGOOD_PP3300_PCH,
	X86_PGOOD_PP5000,
	X86_PGOOD_S5,
	X86_PGOOD_VCORE,
	X86_PGOOD_PP1000_S0IX,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
#ifdef CONFIG_CHIPSET_DEBUG
	X86_SLP_SX_DEASSERTED,
	X86_SUS_STAT_ASSERTED,
	X86_SUSPWRDNACK_ASSERTED,
#endif

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum adc_channel {
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,

	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_LED_GREEN,
	PWM_CH_LED_RED,

	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum temp_sensor_id {
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL = 0,

	/* TMP432 local and remote sensors */
	TEMP_SENSOR_I2C_TMP432_LOCAL,
	TEMP_SENSOR_I2C_TMP432_REMOTE1,
	TEMP_SENSOR_I2C_TMP432_REMOTE2,

	/* Battery temperature sensor */
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
