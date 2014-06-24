/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Snow board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000

/* Debug features */
#undef CONFIG_TASK_PROFILING

/* Optional features */
#define CONFIG_BATTERY_BQ20Z453
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CHARGER_TPS65090
#define CONFIG_CHIPSET_GAIA
#define CONFIG_CMD_PMU
#define CONFIG_EXTPOWER_SNOW
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_I2C_ARBITRATION
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_KEYBOARD_SUPPRESS_NOISE
#define CONFIG_PMU_HARD_RESET
#define CONFIG_PMU_TPS65090
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH

/* use STOP mode when we have nothing to do */
#define CONFIG_LOW_POWER_IDLE

#ifndef __ASSEMBLER__

/* Keyboard output ports */
#define KB_OUT_PORT_LIST GPIO_B, GPIO_C

/* Charging */
#define I2C_PORT_MASTER 1
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_SLAVE 1

#define GPIO_AP_CLAIM	GPIO_SPI1_NSS	/* AP claims bus */
#define GPIO_EC_CLAIM	GPIO_SPI1_MISO	/* EC claims bus */

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 4
#define TIM_POWER_LED 2
#define TIM_WATCHDOG  1

#include "gpio_signal.h"

enum pwm_channel {
	PWM_CH_POWER_LED = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
