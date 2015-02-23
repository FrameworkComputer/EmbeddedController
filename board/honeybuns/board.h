/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_HW_CRC
#define CONFIG_I2C
#undef CONFIG_LID_SWITCH
#define CONFIG_STM_HWTIMER32
#undef CONFIG_TASK_PROFILING
#define CONFIG_USB
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#undef CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#undef CONFIG_WATCHDOG_HELP



/* I2C ports configuration */
#define I2C_PORT_MASTER 0

/* USB configuration */
#define CONFIG_USB_PID 0x5015
/* By default, enable all console messages excepted USB */
#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_USB))

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
	ADC_CH_VIN_DIV_P,
	ADC_CH_VIN_DIV_N,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_BB_URL,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_COUNT        0

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_COUNT		1

#endif /* __BOARD_H */
