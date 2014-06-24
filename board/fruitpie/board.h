/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fruitpie board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_BATTERY_SMART
#define CONFIG_USB_SWITCH_TSU6721
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_SLAVE  0

/* Charger configuration */
#define CONFIG_CHARGER
#undef  CONFIG_CHARGER_V1
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_SENSE_RESISTOR     5 /* milliOhms */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* milliOhms */
#define CONFIG_CHARGER_INPUT_CURRENT    512 /* mA */
#define CONFIG_CHARGER_ILIM_PIN_DISABLED    /* external ILIM pin disabled */

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_CH_CC1_PD = 0,
	ADC_CH_CC2_PD,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Muxing for the USB type C */
enum typec_mux {
	TYPEC_MUX_NONE,
	TYPEC_MUX_USB1,
	TYPEC_MUX_USB2,
	TYPEC_MUX_DP1,
	TYPEC_MUX_DP2,
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
