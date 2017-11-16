/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Grunt board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_HIBERNATE_PSL
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands. */

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* No tach. */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
/* Flash is 1MB but reserve half for future use. */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Modules */
#define CONFIG_ADC
#define CONFIG_I2C
#define CONFIG_LPC

#define CONFIG_BOARD_VERSION

#define CONFIG_CHIPSET_STONEY
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_UART_HOST 0

#define CONFIG_I2C_MASTER

#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_DOWN_L GPIO_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL

#define I2C_PORT_BATTERY	I2C_PORT_POWER
#define I2C_PORT_POWER		NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC0		NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT2_0
#define I2C_PORT_THERMAL	NPCX_I2C_PORT3_0
#define I2C_PORT_SENSOR		NPCX_I2C_PORT7_0

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_SOC,
	ADC_CH_COUNT
};

enum power_signal {
	X86_SLP_S3_N,
	X86_SLP_S5_N,
	X86_VGATE,
	X86_SPOK,
	POWER_SIGNAL_COUNT
};

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
