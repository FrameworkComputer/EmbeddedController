/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tigertail configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Enable USART1 USB streams */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* The UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x5027
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_UPDATE

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE	0
#define USB_IFACE_UPDATE	1
#define USB_IFACE_USART1_STREAM	2
#define USB_IFACE_I2C		3
#define USB_IFACE_COUNT		4

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_CONSOLE		1
#define USB_EP_UPDATE		2
#define USB_EP_USART1_STREAM	3
#define USB_EP_I2C		4
#define USB_EP_COUNT		5

/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_MASTER 0
#define CONFIG_INA231

/* Enable ADC */
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
	USB_STR_USART1_STREAM_NAME,
	USB_STR_CONSOLE_NAME,
	USB_STR_UPDATE_NAME,

	USB_STR_COUNT
};

/* ADC signal */
enum adc_channel {
	ADC_SBU1 = 0,
	ADC_SBU2,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

void set_uart_state(int state);

enum uart_states {
	UART_OFF = 0,
	UART_ON_PP1800,
	UART_FLIP_PP1800,
	UART_ON_PP3300,
	UART_FLIP_PP3300,
	UART_AUTO,
};

enum uart_detect_states {
	UART_DETECT_OFF = 0,
	UART_DETECT_AUTO,
};

void set_mux_state(int state);
enum mux_states {
	MUX_OFF = 0,
	MUX_A,
	MUX_B,
};

void button_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
