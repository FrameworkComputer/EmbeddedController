/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* llama board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_CHIPSET_MEDIATEK
/* Add for AC adaptor, charger, battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24715
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_V2
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_COMMON
/* #define CONFIG_PWM */
#define CONFIG_SPI
#define CONFIG_STM_HWTIMER32
#define CONFIG_VBOOT_HASH
#undef CONFIG_WATCHDOG_HELP
#define CONFIG_LID_SWITCH
#define CONFIG_SWITCH
#define CONFIG_BOARD_VERSION
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* Hibernate is not supported on STM32F0.*/
#undef CONFIG_HIBERNATE
/* #define CONFIG_HIBERNATE_WAKEUP_PINS STM32_PWR_CSR_EWUP1 */

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#define CONFIG_PMIC_FW_LONG_PRESS_TIMER
/* Optional features */
/* #define CONFIG_HW_CRC */
#define CONFIG_CMD_HOSTCMD

#ifndef __ASSEMBLER__

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER 0
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 4

#include "gpio_signal.h"

enum power_signal {
	MTK_POWER_GOOD = 0,
	MTK_SUSPEND_ASSERTED,
	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

enum pwm_channel {
	PWM_CH_POWER_LED = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* Charger module */
/* Charge sense resistor */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* mOhm */
/* Input sensor resistor */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* mOhm */
#define CONFIG_CHARGER_INPUT_CURRENT 2150 /* mA */

/* Set AP reset pin according to parameter */
void board_set_ap_reset(int asserted);

#endif				/* !__ASSEMBLER__ */

#endif				/* __CROS_EC_BOARD_H */
