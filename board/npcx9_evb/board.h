/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Nuvoton M4 EB */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* EC modules */
#define CONFIG_ADC
#define CONFIG_PWM
#define CONFIG_SPI
#define CONFIG_I2C
/* Features of eSPI */
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S3
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S4

/* Optional features */
#define CONFIG_ENABLE_JTAG_SELECTION
#define CONFIG_BOARD_VERSION_GPIO
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#undef  CONFIG_LOW_POWER_IDLE /* Deep Sleep Support */
#define CONFIG_POWER_BUTTON
#undef  CONFIG_PSTORE
#define CONFIG_PWM_KBLIGHT
#define CONFIG_VBOOT_HASH
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands */

/* EC console commands */
#define CONFIG_CMD_TASKREADY
#define CONFIG_CMD_STACKOVERFLOW
#define CONFIG_CMD_JUMPTAGS
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_I2CWEDGE

/* I2C port for CONFIG_CMD_I2CWEDGE */
#define I2C_PORT_MASTER NPCX_I2C_PORT0_0
#define I2C_PORT_HOST   0

/* Fans for testing */
#define CONFIG_FANS 1

#define CONFIG_KEYBOARD_KSO_HIGH_DRIVE /* Quasi-bidirectional buf for KSOs */
#define CONFIG_HIBERNATE_PSL /* Use PSL (Power Switch Logic) for hibernate */
#undef CONFIG_CLOCK_SRC_EXTERNAL /* Use external 32kHz OSC as LFCLK source */

/* Optional feature to configure npcx9 chip */

/* Select which UART Controller is the Console UART */
#undef CONFIG_CONSOLE_UART
#define CONFIG_CONSOLE_UART    0 /* 0:UART1 1:UART2 */
/*
 * This definition below actually doesn't define which UART controller to be
 * used. Instead, it defines which pinouts (GPIO10/11 or GPIO64/65) are
 * connected to "UART1" controller.
 */
#define NPCX_UART_MODULE2  1 /* 1:GPIO64/65 as UART1 */
#define NPCX_TACH_SEL2     0 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */
#define NPCX9_PWM1_SEL     0 /* 0:GPIOC2 as I2CSCL0 1:as PWM1 */

#ifndef __ASSEMBLER__

enum adc_channel {
	ADC_CH_0 = 0,
	ADC_CH_1,
	ADC_CH_2,
	ADC_CH_3,
	ADC_CH_4,
	ADC_CH_5,
	ADC_CH_6,
	ADC_CH_7,
	ADC_CH_8,
	ADC_CH_9,
	ADC_CH_10,
	ADC_CH_11,
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_FAN,
	PWM_CH_KBLIGHT,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0,
	/* Number of FAN channels */
	FAN_CH_COUNT
};

enum mft_channel {
	MFT_CH_0,
	/* Number of MFT channels */
	MFT_CH_COUNT
};

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
