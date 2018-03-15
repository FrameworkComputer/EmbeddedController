/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cheza board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* TODO(waihong): Remove the following bringup features */
#define CONFIG_BRINGUP
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands. */

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* No tach. */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE (512 * 1024) /* It's really 1MB. */
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Modules */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_ADC
#undef CONFIG_PWM
#undef CONFIG_PECI

#define CONFIG_BOARD_VERSION
#define CONFIG_POWER_BUTTON
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_SWITCH
#define CONFIG_LID_SWITCH
#define CONFIG_EXTPOWER_GPIO

#define CONFIG_CHIPSET_SDM845
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_POWER_COMMON

/* NPCX Features */
#define CONFIG_HIBERNATE_PSL

/* I2C Ports */
#define I2C_PORT_BATTERY I2C_PORT_POWER
#define I2C_PORT_CHARGER I2C_PORT_POWER
#define I2C_PORT_POWER   NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC0   NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1   NPCX_I2C_PORT2_0
#define I2C_PORT_EEPROM  NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR  NPCX_I2C_PORT7_0

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum power_signal {
	SDM845_POWER_GOOD = 0,
	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

enum adc_channel {
	ADC_VBUS = -1,
	ADC_CH_COUNT
};

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BOARD_H */
