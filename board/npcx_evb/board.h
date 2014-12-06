/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Nuvoton M4 EB */

#ifndef __BOARD_H
#define __BOARD_H

/* Support Code RAM architecture (Run code in RAM) */
#define CONFIG_CODERAM_ARCH

/* Optional modules */
#define CONFIG_ADC
#define CONFIG_PECI
#define CONFIG_PWM
#define CONFIG_SPI

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands for testing */
#define CONFIG_SPI_FLASH
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_POWER_BUTTON
#define CONFIG_VBOOT_HASH
#define CONFIG_PWM_KBLIGHT
#define CONFIG_BOARD_VERSION

/* Optional features for test commands */
#define CONFIG_CMD_TASKREADY
#define CONFIG_CMD_STACKOVERFLOW
#define CONFIG_CMD_JUMPTAGS
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_I2CWEDGE

#define CONFIG_UART_HOST                0
#define CONFIG_FANS                     1
#define CONFIG_SPI_FLASH_SIZE           0x00800000 /* 8MB spi flash */

/* Optional feature - used by nuvoton */
#define CONFIG_PWM_INPUT_LFCLK          /* PWM use LFCLK for input clock */
#define CONFIG_MFT_INPUT_LFCLK          /* MFT use LFCLK for input clock */

/* Optional for testing */
#undef  CONFIG_PSTORE
#define CONFIG_LOW_POWER_IDLE           /* Deep Sleep Support */

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER         0
#define I2C_PORT_HOST           0

#ifndef __ASSEMBLER__

enum adc_channel {
	ADC_CH_0 = 0,
	ADC_CH_1,
	ADC_CH_2,
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

#endif /* __BOARD_H */
