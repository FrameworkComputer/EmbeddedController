/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32F072-discovery board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x500f

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_STREAM 0
#define USB_IFACE_GPIO   1
#define USB_IFACE_SPI    2
#define USB_IFACE_COUNT  3

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_STREAM  1
#define USB_EP_GPIO    2
#define USB_EP_SPI     3
#define USB_EP_COUNT   4

/* Enable control of GPIOs over USB */
#define CONFIG_USB_GPIO

/* Enable control of SPI over USB */
#define CONFIG_SPI_MASTER_PORT 2
#define CONFIG_SPI_CS_GPIO     GPIO_SPI_CS

#define CONFIG_USB_SPI

#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_STREAM_NAME,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __BOARD_H */
