/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dingdong dongle configuration */

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
#define CONFIG_HW_CRC
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

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
