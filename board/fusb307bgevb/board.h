/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fusb307bgevb configuration */

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
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x1234
#define CONFIG_USB_CONSOLE

/* I2C master port connected to the TCPC */
#define I2C_PORT_TCPC 0

/* LCD Configuration */
#define LCD_SLAVE_ADDR 0x27

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_STREAM  0
#define USB_IFACE_GPIO    1
#define USB_IFACE_SPI     2
#define USB_IFACE_CONSOLE 3
#define USB_IFACE_COUNT   4

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_STREAM  1
#define USB_EP_GPIO    2
#define USB_EP_SPI     3
#define USB_EP_CONSOLE 4
#define USB_EP_COUNT   5

/* Enable control of GPIOs over USB */
#define CONFIG_USB_GPIO

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

	USB_STR_COUNT
};

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
