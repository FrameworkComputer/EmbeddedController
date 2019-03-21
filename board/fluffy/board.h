/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fluffy configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* This is not an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_PID 0x503b
#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE	0
#define USB_IFACE_COUNT		1

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_CONSOLE		1
#define USB_EP_COUNT		2

/* Optional features */
#define CONFIG_STM_HWTIMER32

#define CONFIG_ADC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

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
	USB_STR_CONSOLE_NAME,
	USB_STR_COUNT
};

enum adc_channel {
	ADC_PPVAR_VBUS_DUT,
	ADC_CH_COUNT,
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
