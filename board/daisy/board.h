/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Daisy board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000

/* Use USART1 as console serial port */
#define CONFIG_CONSOLE_UART 1

#define CONFIG_SPI

#define USB_CHARGE_PORT_COUNT 0

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_EC_PWRON = 0,     /* Power button */
	GPIO_PP1800_LDO2,      /* LDO2 is ON (end of PMIC sequence) */
	GPIO_SOC1V8_XPSHOLD,   /* App Processor ON  */
	GPIO_CHARGER_INT,
	GPIO_LID_OPEN,         /* LID switch detection */
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
	GPIO_SPI1_NSS,
	/* Outputs */
	GPIO_EN_PP1350,        /* DDR 1.35v rail enable */
	GPIO_EN_PP5000,        /* 5.0v rail enable */
	GPIO_EN_PP3300,        /* 3.3v rail enable */
	GPIO_PMIC_ACOK,        /* 5v rail ready */
	GPIO_EC_ENTERING_RW,   /* EC is R/W mode for the kbc mux */
	GPIO_CHARGER_EN,
	GPIO_EC_INT,

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

void configure_board(void);

void matrix_interrupt(enum gpio_signal signal);

/* Signal to the AP that keyboard scan data is available */
void board_keyboard_scan_ready(void);

#endif /* __BOARD_H */
