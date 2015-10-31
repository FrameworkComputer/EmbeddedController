/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pit board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_BATTERY_BQ20Z453
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGER_TPS65090
#define CONFIG_CHIPSET_GAIA
#define CONFIG_CMD_PMU
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_PMU_HARD_RESET
#define CONFIG_PMU_POWERINFO
#define CONFIG_PMU_TPS65090
#define CONFIG_PMU_TPS65090_CHARGING_LED
#define CONFIG_SPI
#define CONFIG_SPI_PROTOCOL_V2
#define CONFIG_VBOOT_HASH

#ifndef __ASSEMBLER__

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER 0
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER

/* Charger sense resistors */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 12
#define CONFIG_CHARGER_SENSE_RESISTOR 16

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 9
#define TIM_POWER_LED 2
#define TIM_WATCHDOG  4

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
