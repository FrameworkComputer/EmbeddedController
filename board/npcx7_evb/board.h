/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Nuvoton M4 EB */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * npcx7 EVB version:
 * 1 - for EVB version 1 which supports npcx7m6g
 * 2 - for EVB version 2 which supports npcx7m6f/npcx7m6fb/npcx7m6fc/npcx7m7wb
 */
#if defined(CHIP_VARIANT_NPCX7M6G)
#define BOARD_VERSION  1
#elif defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M6FC) || defined(CHIP_VARIANT_NPCX7M7WB) || \
	defined(CHIP_VARIANT_NPCX7M7WC)
#define BOARD_VERSION  2
#endif

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

/* Internal spi-flash on npcx7 ec */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH
#define CONFIG_SPI_FLASH_REGS
#if defined(CHIP_VARIANT_NPCX7M6FC) || defined(CHIP_VARIANT_NPCX7M7WC)
#define CONFIG_SPI_FLASH_W25Q40 /* Internal spi flash type */
#define CONFIG_FLASH_SIZE 0x00080000 /* 512 KB internal spi flash */
#else
#define CONFIG_SPI_FLASH_W25Q80 /* Internal spi flash type */
#define CONFIG_FLASH_SIZE 0x00100000 /* 1 MB internal spi flash */
#endif

/* New features on npcx7 ec */
#define CONFIG_KEYBOARD_KSO_HIGH_DRIVE /* Quasi-bidirectional buf for KSOs */
#if (BOARD_VERSION == 2)
#define CONFIG_HIBERNATE_PSL /* Use PSL (Power Switch Logic) for hibernate */
#define CONFIG_CLOCK_SRC_EXTERNAL /* Use external 32kHz OSC as LFCLK source */
#if defined(CHIP_VARIANT_NPCX7M7WB) || defined(CHIP_VARIANT_NPCX7M7WC)
#define CONFIG_WAKE_ON_VOICE /* Use Audio front-end for Wake-on-Voice */
#endif
#undef CONFIG_FANS /* Remove fan application */
#define CONFIG_FANS 0
#else
#undef CONFIG_HIBERNATE_PSL /* Use PSL (Power Switch Logic) for hibernate */
#undef CONFIG_CLOCK_SRC_EXTERNAL /* Use external 32kHz OSC as LFCLK source */
#endif

/* Optional feature to configure npcx7 chip */

/* Select which UART Controller is the Console UART */
#undef CONFIG_CONSOLE_UART
#define CONFIG_CONSOLE_UART    0 /* 0:UART1 1:UART2 */
/*
 * This definition below actually doesn't define which UART controller to be
 * used. Instead, it defines which pinouts (GPIO10/11 or GPIO64/65) are
 * connected to "UART1" controller.
 */
#if (BOARD_VERSION == 2)
#define NPCX_UART_MODULE2  1 /* 1:GPIO64/65 as UART1 */
#else
#define NPCX_UART_MODULE2  0 /* 0:GPIO10/11 as UART1 */
#endif
#define NPCX_JTAG_MODULE2  0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG */
#define NPCX_TACH_SEL2     0 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */
#define NPCX7_PWM1_SEL     0 /* 0:GPIOC2 as I2CSCL0 1:as PWM1 (only in npcx7) */

#ifndef __ASSEMBLER__

enum adc_channel {
	ADC_CH_0 = 0,
	ADC_CH_1,
	ADC_CH_2,
	ADC_CH_3,
	ADC_CH_4,
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
