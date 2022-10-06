/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32F072-discovery board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x500f
#define CONFIG_USB_CONSOLE

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_STREAM 0
#define USB_IFACE_GPIO 1
#define USB_IFACE_SPI 2
#define USB_IFACE_CONSOLE 3
#define USB_IFACE_COUNT 4

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_STREAM 1
#define USB_EP_GPIO 2
#define USB_EP_SPI 3
#define USB_EP_CONSOLE 4
#define USB_EP_COUNT 5

/* Enable control of GPIOs over USB */
#define CONFIG_USB_GPIO

/* Enable control of SPI over USB */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FLASH_PORT 0 /* First SPI controller port */

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
	USB_STR_CONSOLE_NAME,
	USB_STR_SPI_NAME,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
