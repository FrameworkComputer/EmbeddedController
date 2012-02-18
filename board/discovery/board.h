/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32L Discovery board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000

/* Use USART3 as console serial port */
#define CONFIG_CONSOLE_UART 1

#define USB_CHARGE_PORT_COUNT 0

/* Host connects to keyboard controller module via I2C */
#define HOST_KB_BUS_I2C

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_USER_BUTTON = 0,     /* Blue user button */
	/* Keyboard inputs */
	KB_COL00,
	KB_COL01,
	KB_COL02,
	KB_COL03,
	KB_COL04,
	KB_COL05,
	KB_COL06,
	KB_COL07,
	/* Other inputs */
	/* Outputs */
	GPIO_BLUE_LED,            /* Blue debug LED */
	GPIO_GREEN_LED,           /* Green debug LED */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

void configure_board(void);

void matrix_interrupt(enum gpio_signal signal);

#endif /* __BOARD_H */
