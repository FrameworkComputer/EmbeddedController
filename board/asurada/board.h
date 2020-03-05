/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Asurada development board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_S0
#define CONFIG_POWER_BUTTON
#define CONFIG_PWM

/* Battery */
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_FUEL_GAUGE

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

/* Keyboard */
/*
 * #define CONFIG_KEYBOARD_BOARD_CONFIG
 * #define CONFIG_KEYBOARD_PROTOCOL_MKBP
 * #define CONFIG_MKBP_USE_GPIO
 */

/* PD */

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

/* UART */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

#define I2C_PORT_CHARGER IT83XX_I2C_CH_C
#define I2C_PORT_BATTERY IT83XX_I2C_CH_C

#include "gpio_signal.h"

enum battery_type {
	BATTERY_DUMMY,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_COUNT,
};

enum adc_channel {
	ADC_BOARD_ID_0,
	ADC_BOARD_ID_1,
	/* Number of ADC channels */
	ADC_CH_COUNT,
};

int board_get_version(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
