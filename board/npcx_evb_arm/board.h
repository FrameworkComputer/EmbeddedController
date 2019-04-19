/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Nuvoton M4 EB */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional modules */
#define CONFIG_ADC
#define CONFIG_PWM
#define CONFIG_HOSTCMD_SPS /* Used in ARM-based platform for host interface */

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands for testing */
#define CONFIG_FLASH_SIZE          0x00800000 /* 8MB spi flash */
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q64
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_MKBP /* Instead of 8042 protocol of keyboard */
#define CONFIG_MKBP_USE_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_VBOOT_HASH
#define CONFIG_PWM_KBLIGHT
#define CONFIG_BOARD_VERSION_GPIO
#define CONFIG_ENABLE_JTAG_SELECTION

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

/* Optional feature - used by nuvoton */
#define NPCX_UART_MODULE2    0 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */
/* Enable SHI PU on transition to S0. Disable the PU otherwise for leakage. */
#define NPCX_SHI_CS_PU
/* Enable bypass since shi outputs invalid data when across 256B boundary */
#define NPCX_SHI_BYPASS_OVER_256B

/* Optional for testing */
#undef  CONFIG_PSTORE
#undef  CONFIG_LOW_POWER_IDLE           /* Deep Sleep Support */

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER         NPCX_I2C_PORT0_0
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

#endif /* __CROS_EC_BOARD_H */
