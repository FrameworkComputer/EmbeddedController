/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Asurada board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Chipset config */
#define CONFIG_BRINGUP
#define CONFIG_CHIPSET_MT8192
#define CONFIG_CMD_POWERINDEBUG
#define CONFIG_POWER_COMMON

/* Optional features */
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_S0
#define CONFIG_POWER_BUTTON
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_WP_ACTIVE_HIGH

/* Battery */
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_ODL

/* BC12 */
/* #define CONFIG_BC12_DETECT_PI3USB9201 */

/* Charger */
#define CONFIG_CHARGER
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* BOARD_RS1 */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* BOARD_RS2 */
#define CONFIG_CHARGER_OTG
#define CONFIG_CHARGE_RAMP_HW

/* Chipset */

/* Keyboard */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_USE_GPIO

/* I2C */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_CHARGER IT83XX_I2C_CH_A
#define I2C_PORT_BATTERY IT83XX_I2C_CH_A

/* PD / USB-C */

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

/* Sensor */
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_TABLET_MODE
#define GMR_TABLET_MODE_GPIO_L GPIO_TABLET_MODE_L

/* UART */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* GPIO name remapping */
#define GPIO_EN_HDMI_PWR        GPIO_EC_X_GPIO1
#define GPIO_USB_C1_FRS_EN      GPIO_EC_X_GPIO1
#define GPIO_USB_C1_PPC_INT_ODL GPIO_X_EC_GPIO2
#define GPIO_PS185_EC_DP_HPD    GPIO_X_EC_GPIO2
#define GPIO_USB_C1_DP_IN_HPD   GPIO_EC_X_GPIO3
#define GPIO_PS185_PWRDN_ODL    GPIO_EC_X_GPIO3

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_C235,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_COUNT,
};

enum adc_channel {
	ADC_TEMP_SENSOR_SUBPMIC, /* ADC 0 */
	ADC_BOARD_ID_0,          /* ADC 1 */
	ADC_BOARD_ID_1,          /* ADC 2 */
	ADC_TEMP_SENSOR_AMB,     /* ADC 3 */
	ADC_TEMP_SENSOR_CHARGER, /* ADC 5 */
	ADC_CHARGER_PMON,        /* ADC 6 */
	ADC_TEMP_SENSOR_AP,      /* ADC 7 */

	/* Number of ADC channels */
	ADC_CH_COUNT,
};

enum power_signal {
	PMIC_PWR_GOOD,
	AP_IN_S3_L,
	AP_WDT_ASSERTED,
	POWER_SIGNAL_COUNT,
};

enum board_sub_board {
	SUB_BOARD_NONE = -1,
	SUB_BOARD_TYPEC,
	SUB_BOARD_HDMI,

	SUB_BOARD_COUNT,
};

int board_get_version(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
