/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* C2D2 configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

#define CONFIG_BOARD_PRE_INIT

/* Enable USART */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1	/* EC USART */
#define CONFIG_STREAM_USART3	/* AP USART - not connected by default */
#define CONFIG_STREAM_USART4	/* H1 USART */
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* The UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_PID 0x5041
#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"
#define CONFIG_USB_UPDATE


/* USB interface indexes (use define rather than enum to expand them)
 *
 * Note these values are used in servo_interface.py for the 'interface' value
 */
#define USB_IFACE_USART4_STREAM	0	/* H1 */
#define USB_IFACE_UPDATE	1
#define USB_IFACE_SPI		2
#define USB_IFACE_CONSOLE	3
#define USB_IFACE_I2C		4
#define USB_IFACE_USART3_STREAM	5	/* AP (not connected by default) */
#define USB_IFACE_USART1_STREAM	6	/* EC */
#define USB_IFACE_COUNT		7

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_USART4_STREAM	1
#define USB_EP_UPDATE		2
#define USB_EP_SPI		3
#define USB_EP_CONSOLE		4
#define USB_EP_I2C		5
#define USB_EP_USART3_STREAM	6
#define USB_EP_USART1_STREAM	7
#define USB_EP_COUNT		8

/* Enable control of SPI over USB */
#define CONFIG_USB_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_FLASH_PORT 0  /* SPI2 is 0th in stm's SPI_REGS var */

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_EC 0
#define I2C_PORT_AUX 1

/* See i2c_ite_flash_support.c for more information about these values */
#define CONFIG_ITE_FLASH_SUPPORT
#define CONFIG_I2C_XFER_LARGE_READ
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT ((1<<9) - 4)
#define CONFIG_USB_I2C_MAX_READ_COUNT ((1<<9) - 6)

/*
 * Set all ADC samples to take 239.5 clock cycles. This allows us to measure
 * weakly driven signals like the H1 Vref.
 */
#define CONFIG_ADC_SAMPLE_TIME STM32_ADC_SMPR_239_5_CY

/* Options features */
#define CONFIG_ADC
/*
 * See 'Programmable voltage detector characteristics' in the STM32F072x8
 * Datasheet. PVD Threshold 1 corresponds to a falling voltage threshold of
 * min:2.09V, max:2.27V.
 */
#define CONFIG_PVD
#define PVD_THRESHOLD 1

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_USART4_STREAM_NAME,
	USB_STR_UPDATE_NAME,
	USB_STR_CONSOLE_NAME,
	USB_STR_I2C_NAME,
	USB_STR_USART3_STREAM_NAME,
	USB_STR_USART1_STREAM_NAME,
	USB_STR_COUNT
};

enum adc_channel {
	ADC_H1_SPI_VREF, /* Either H1 Vref or SPI Vref depending on mode */
	ADC_EC_SPI_VREF, /* Either EC Vref or SPI Vref depending on mode */
	ADC_CH_COUNT,
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
