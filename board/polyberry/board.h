/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Polyberry configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_FLASH_WRITE_SIZE STM32_FLASH_WRITE_SIZE_3300

/* Use external clock */
#define CONFIG_STM32_CLOCK_HSE_HZ 24000000

#define CONFIG_BOARD_POST_GPIO_INIT

/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* The UART console is on test points USART3 (PC10/PC11) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 3
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096
/* Don't waste precious DMA channels on console. */
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

#define CONFIG_UART_TX_REQ_CH 4
#define CONFIG_UART_RX_REQ_CH 4

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x5020
#define CONFIG_USB_CONSOLE
#define CONFIG_STREAM_USB
#define CONFIG_USB_UPDATE

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE	0
#define USB_IFACE_UPDATE	1
#define USB_IFACE_COUNT		2

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_CONSOLE		1
#define USB_EP_UPDATE		2
#define USB_EP_COUNT		3

/* This is not actually a Chromium EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_WATCHDOG

/* Optional features */
#define CONFIG_STM_HWTIMER32

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 5

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,
	USB_STR_UPDATE_NAME,
	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
