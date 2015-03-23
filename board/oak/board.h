/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* oak board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Add for AC adaptor, charger, battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 2150
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_V2
#define CONFIG_CHIPSET_MEDIATEK
#define CONFIG_FORCE_CONSOLE_RESUME
#undef  CONFIG_HIBERNATE
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_PMIC_FW_LONG_PRESS_TIMER
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_COMMON
#define CONFIG_SPI
#define CONFIG_STM_HWTIMER32
#define CONFIG_VBOOT_HASH
#undef  CONFIG_WATCHDOG_HELP
#define CONFIG_LID_SWITCH
#define CONFIG_SWITCH
#define CONFIG_BOARD_VERSION
#undef  CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* UART DMA */
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Optional features */
#define CONFIG_CMD_HOSTCMD

/* Drivers */
#define CONFIG_USB_SWITCH_PI3USB30532
/*
 * 8-bit USB type-C switch I2C addresses:
 *   port 0: 0x54 << 1
 *   port 1: 0x55 << 1
 */
#define CONFIG_USB_SWITCH_I2C_ADDRS {0x54 << 1, 0x55 << 1}

#ifndef __ASSEMBLER__

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C, GPIO_D

/* 2 I2C master ports, connect to battery, charger, pd and USB switches */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0
#define I2C_PORT_PD_MCU 1
#define I2C_PORT_USB_SWITCH 1

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

/* Discharge battery when on AC power for factory test. */
int board_discharge_on_ac(int enable);

/* Reset PD MCU */
void board_reset_pd_mcu(void);

#endif  /* !__ASSEMBLER__ */

#endif  /* __BOARD_H */
