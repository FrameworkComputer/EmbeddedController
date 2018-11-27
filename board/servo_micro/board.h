/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Servo micro configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

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
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC

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
#define USB_IFACE_USART4_STREAM	0
#define USB_IFACE_UPDATE	1
#define USB_IFACE_SPI		2
#define USB_IFACE_CONSOLE	3
#define USB_IFACE_I2C		4
#define USB_IFACE_USART3_STREAM	5
#define USB_IFACE_USART2_STREAM	6
#define USB_IFACE_COUNT		7

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_USART4_STREAM	1
#define USB_EP_UPDATE		2
#define USB_EP_SPI		3
#define USB_EP_CONSOLE		4
#define USB_EP_I2C		5
#define USB_EP_USART3_STREAM	6
#define USB_EP_USART2_STREAM	7
#define USB_EP_COUNT		8

/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* Enable control of SPI over USB */
#define CONFIG_USB_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_FLASH_PORT    0  /* First SPI master port */

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
/*
 * iteflash requires 256 byte reads for verifying ITE EC firmware.  Without this
 * the limit is CONFIG_I2C_CHIP_MAX_READ_SIZE which is 255 for STM32F0 due to an
 * 8 bit field, per src/platform/ec/include/config.h comment.
 */
#define CONFIG_I2C_XFER_LARGE_READ
#define I2C_PORT_MASTER 0

/*
 * As of 2018-11-27 the default for both is 60 bytes.  These larger values allow
 * for reflashing of ITE EC chips over I2C
 * (https://issuetracker.google.com/79684405) in reasonably speedy fashion.  If
 * the EC firmware defaults are ever raised significantly, consider removing
 * these overrides.
 *
 * As of 2018-11-27 the actual maximum write size supported by the I2C-over-USB
 * protocol is (1<<12)-1, and the maximum read size supported is
 * (1<<15)-1.  However compile time assertions require that these values be
 * powers of 2 after overheads are included.  Thus, the write limit set here
 * /should/ be (1<<12)-4 and the read limit should be (1<<15)-6, however those
 * ideal limits are not actually possible because servo_micro lacks sufficient
 * spare memory for them.  With symmetrical limits, the maximum that currently
 * fits is (1<<11)-4 write limit and (1<<11)-6 read limit, leaving 1404 bytes of
 * RAM available.
 *
 * However even with a sufficiently large write value here, the maximum that
 * actually works as of 2018-12-03 is 255 bytes.  Additionally, ITE EC firmware
 * image verification requires exactly 256 byte reads.  Thus the only useful
 * powers-of-2-minus-overhead limits to set here are (1<<9)-4 writes and
 * (1<<9)-6 reads, leaving 6012 bytes of RAM available, down from 7356 bytes of
 * RAM available with the default 60 byte limits.
 */
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT ((1<<9) - 4)
#define CONFIG_USB_I2C_MAX_READ_COUNT ((1<<9) - 6)

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
