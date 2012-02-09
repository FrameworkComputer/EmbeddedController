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

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_USER_BUTTON = 0,     /* Blue user button */
	/* Other inputs */
	/* Outputs */
	GPIO_BLUE_LED,            /* Blue debug LED */
	GPIO_GREEN_LED,           /* Green debug LED */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

void configure_board(void);

#endif /* __BOARD_H */
