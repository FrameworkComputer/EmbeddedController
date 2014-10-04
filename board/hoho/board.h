/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Twinkie dongle configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_ADC
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_SPI_FLASH
#define CONFIG_SPI_FLASH_SIZE 1048576
#define CONFIG_SPI_MASTER_PORT 2
#define CONFIG_SPI_CS_GPIO GPIO_PD_MCDP_SPI_CS_L
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* I2C ports configuration */
#define I2C_PORT_MASTER 0

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
	/* Number of ADC channels */
	ADC_CH_COUNT
};
#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
