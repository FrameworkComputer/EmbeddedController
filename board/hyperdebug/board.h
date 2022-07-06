/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* HyperDebug configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_LTO

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

#define CONFIG_BOARD_PRE_INIT

#define CONFIG_ROM_BASE 0x0
#define CONFIG_ROM_SIZE (CONFIG_RAM_BASE - CONFIG_ROM_BASE)

/* Enable USB forwarding on UART 1, 2, 4 and the LPUART (UART9) */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART2
#undef CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#undef CONFIG_STREAM_USART5
#define CONFIG_STREAM_USART9
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* The UART console is on UART3 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 3
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC
#undef CONFIG_PVD
/*
 * See 'Programmable voltage detector characteristics' in the
 * STM32F072x8 Datasheet.  PVD Threshold 1 corresponds to a falling
 * voltage threshold of min:2.09V, max:2.27V.
 */
#define PVD_THRESHOLD (1)

/* USB Configuration */

#define CONFIG_USB
#define CONFIG_USB_PID 0x520e
#define CONFIG_USB_CONSOLE

/*
 * Enabling USB updating would exceed the number of USB endpoints
 * supported by the hardware.  We will have to rely on the built-in
 * DFU support of STM32 chips.
 */
#undef CONFIG_USB_UPDATE

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_SPI 1
#define USB_IFACE_I2C 2
#define USB_IFACE_USART1_STREAM 3
#define USB_IFACE_USART2_STREAM 4
#define USB_IFACE_USART4_STREAM 5
#define USB_IFACE_USART9_STREAM 6
#define USB_IFACE_COUNT 7

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_CONSOLE 1
#define USB_EP_SPI 2
#define USB_EP_I2C 3
#define USB_EP_USART1_STREAM 4
#define USB_EP_USART2_STREAM 5
#define USB_EP_USART4_STREAM 6
#define USB_EP_USART9_STREAM 7
#define USB_EP_COUNT 8

/*
 * Do not enable the common EC command gpioset for recasting of GPIO
 * type. Instead, board specific commands are used for implementing
 * the OpenTitan tool requirements.
 */
#undef CONFIG_CMD_GPIO_EXTENDED
#define CONFIG_GPIO_GET_EXTENDED

/* Enable control of SPI over USB */
#define CONFIG_USB_SPI
#define CONFIG_USB_SPI_BUFFER_SIZE 2048
#define CONFIG_SPI_CONTROLLER

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define I2C_PORT_CONTROLLER 0
#define CONFIG_STM32_SPI1_CONTROLLER

/* See i2c_ite_flash_support.c for more information about these values */
/*#define CONFIG_ITE_FLASH_SUPPORT */
/*#define CONFIG_I2C_XFER_LARGE_TRANSFER */
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT ((1 << 9) - 4)
#define CONFIG_USB_I2C_MAX_READ_COUNT ((1 << 9) - 6)

/* This is not actually an EC so disable some features. */
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
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,
	USB_STR_SPI_NAME,
	USB_STR_I2C_NAME,
	USB_STR_USART1_STREAM_NAME,
	USB_STR_USART2_STREAM_NAME,
	USB_STR_USART4_STREAM_NAME,
	USB_STR_USART9_STREAM_NAME,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
