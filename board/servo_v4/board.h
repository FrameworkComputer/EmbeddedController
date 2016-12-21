/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Servo V4 configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM_USART

#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x501b
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_UPDATE

#define CONFIG_USB_SELF_POWERED

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE	0
#define USB_IFACE_GPIO		1
#define USB_IFACE_I2C		2
#define USB_IFACE_USART3_STREAM	3
#define USB_IFACE_USART4_STREAM	4
#define USB_IFACE_UPDATE	5
#define USB_IFACE_COUNT		6

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_CONSOLE		1
#define USB_EP_GPIO		2
#define USB_EP_I2C		3
#define USB_EP_USART3_STREAM	4
#define USB_EP_USART4_STREAM	5
#define USB_EP_UPDATE		6
#define USB_EP_COUNT		7

/* Enable control of GPIOs over USB */
#define CONFIG_USB_GPIO
/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_MASTER 1

/* PD features */
#define CONFIG_ADC

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

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_I2C_NAME,
	USB_STR_CONSOLE_NAME,
	USB_STR_USART3_STREAM_NAME,
	USB_STR_USART4_STREAM_NAME,
	USB_STR_UPDATE_NAME,

	USB_STR_COUNT
};


/* ADC signal */
enum adc_channel {
	ADC_DUT_CC1_PD = 0,
	ADC_DUT_CC2_PD,
	ADC_CHG_CC1_PD,
	ADC_CHG_CC2_PD,
	ADC_SBU1_DET,
	ADC_SBU2_DET,
	ADC_SUB_C_REF,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
