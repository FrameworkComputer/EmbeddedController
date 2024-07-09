/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Servo micro configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_LTO

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

#define CONFIG_BOARD_PRE_INIT

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART2
#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* The UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/* Optional features */
#define CONFIG_HW_CRC
#define CONFIG_PVD
/* See 'Programmable voltage detector characteristics' in the STM32F072x8
   Datasheet. PVD Threshold 1 corresponds to a falling voltage threshold of
   min:2.09V, max:2.27V. */
#define PVD_THRESHOLD (1)

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x501a
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_UPDATE

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_USART4_STREAM 0
#define USB_IFACE_UPDATE 1
#define USB_IFACE_SPI 2
#define USB_IFACE_CONSOLE 3
#define USB_IFACE_I2C 4
#define USB_IFACE_USART3_STREAM 5
#define USB_IFACE_USART2_STREAM 6
#define USB_IFACE_COUNT 7

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_USART4_STREAM 1
#define USB_EP_UPDATE 2
#define USB_EP_SPI 3
#define USB_EP_CONSOLE 4
#define USB_EP_I2C 5
#define USB_EP_USART3_STREAM 6
#define USB_EP_USART2_STREAM 7
#define USB_EP_COUNT 8

/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* Enable control of SPI over USB */
#define CONFIG_USB_SPI
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FLASH_PORT 0 /* First SPI controller port */

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define CONFIG_CMD_I2C_SPEED
#define I2C_PORT_MASTER 0

/* See i2c_ite_flash_support.c for more information about these values */
#define CONFIG_ITE_FLASH_SUPPORT
#define CONFIG_I2C_XFER_LARGE_TRANSFER
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT ((1 << 9) - 4)
#define CONFIG_USB_I2C_MAX_READ_COUNT ((1 << 9) - 6)

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2

#include "gpio_signal.h"

/* GPIO signal mapping */
#define GPIO_USART4_SERVO_TX_DUT_RX GPIO_UART3_TX_SERVO_JTAG_TCK
#define GPIO_USART4_SERVO_RX_DUT_TX GPIO_UART3_RX_JTAG_BUFFER_TO_SERVO_TDO

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_SPI_NAME,
	USB_STR_I2C_NAME,
	USB_STR_USART4_STREAM_NAME,
	USB_STR_CONSOLE_NAME,
	USB_STR_USART3_STREAM_NAME,
	USB_STR_USART2_STREAM_NAME,
	USB_STR_UPDATE_NAME,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
