/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX7 config */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0
#define I2C_PORT_PPC0		NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT2_0
#define I2C_PORT_TCPC0		NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM		0xA0

#endif /* __CROS_EC_BASEBOARD_H */
