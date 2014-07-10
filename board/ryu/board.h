/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ryu board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* By default, enable all console messages excepted USB */
#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_USBPD))

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#undef  CONFIG_LID_SWITCH
#define CONFIG_VBOOT_HASH
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_TASK_PROFILING

/* Charging configuration */
#undef CONFIG_BATTERY_RYU /* TODO implement */
#define CONFIG_BATTERY_BQ27541
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_SENSE_RESISTOR 5
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_INPUT_CURRENT 2048
#define CONFIG_CHARGER_DISCHARGE_ON_AC

/* I2C ports configuration */
#define I2C_PORT_MASTER 0
#define I2C_PORT_SLAVE  1
#define I2C_PORT_EC I2C_PORT_SLAVE
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_BATTERY I2C_PORT_MASTER

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_VBUS = 0,
	ADC_CC1_PD,
	ADC_CC2_PD,
	ADC_IADP,
	ADC_IBAT,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Discharge battery when on AC power for factory test. */
int board_discharge_on_ac(int enable);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
