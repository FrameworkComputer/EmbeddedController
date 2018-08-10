/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CHIP_G_UART_BITBANG_H
#define __CROS_EC_CHIP_G_UART_BITBANG_H

/* UART Bit Banging */

#include "common.h"
#include "gpio.h"

/* These are functions that we'll have to replace. */
struct uartn_function_ptrs {
	int (*_rx_available)(int uart);
	void (*_write_char)(int uart, char c);
	int (*_read_char)(int uart);
};

/*
 * And these are the function definitions.  The functions live in
 * chip/g/uartn.c.
 */
extern int _uartn_rx_available(int uart);
extern void _uartn_write_char(int uart, char c);
extern int _uartn_read_char(int uart);
extern int _uart_bitbang_rx_available(int uart);
extern void _uart_bitbang_write_char(int uart, char c);
extern int _uart_bitbang_read_char(int uart);
extern struct uartn_function_ptrs uartn_funcs[];

struct uart_bitbang_properties {
	enum gpio_signal tx_gpio;
	enum gpio_signal rx_gpio;
	uint32_t tx_pinmux_reg;
	uint32_t tx_pinmux_regval;
	uint32_t rx_pinmux_reg;
	uint32_t rx_pinmux_regval;
	int baud_rate;
	uint8_t uart;
	struct {
		unsigned int head : 3;
		unsigned int tail : 3;
		unsigned int parity : 2;
	} htp __packed;
};

/* In order to bitbang a UART, a board must define a bitbang_config. */
extern struct uart_bitbang_properties bitbang_config;

/**
 * Configure bit banging mode for a UART.
 *
 * If configuration succeeds, then call uart_bitbang_enable() on the port.
 *
 * @param baud_rate:  desired baud rate.
 * @param parity:  0: no parity, 1: odd parity, 2: even parity.
 *
 * @returns EC_SUCCESS on success, otherwise an error.
 */
int uart_bitbang_config(int baud_rate, int parity);

/**
 * Enable bit banging mode for a UART.
 *
 * The UART must have been configured first.
 */
int uart_bitbang_enable(void);

/**
 * Disable bit banging mode for a UART.
 */
int uart_bitbang_disable(void);

/**
 * Returns 1 if bit banging mode is enabled for the UART.
 */
int uart_bitbang_is_enabled(void);

/**
 * Returns 1 if bit banging mode is wanted for the UART.
 */
int uart_bitbang_is_wanted(void);

/**
 * TX a character on a UART configured for bit banging mode.
 *
 * @param c: Character to send out.
 */
void uart_bitbang_write_char(char c);

/**
 * Sample the RX line on a UART configured for bit banging mode.
 *
 * This is called when a falling edge is seen on the RX line and will attempt to
 * receive a character.  Incoming data with framing errors or parity errors will
 * be discarded.
 *
 * @returns EC_SUCCESS if a character was successfully received, EC_ERROR_CRC if
 *          there was a framing or parity issue.
 */
int uart_bitbang_receive_char(void);

/**
 * Returns 1 if there are characters available for consumption, otherwise 0.
 */
int uart_bitbang_is_char_available(void);

/**
 * Retrieve a character from the bit bang RX buffer.
 */
int uart_bitbang_read_char(void);

#endif /* __CROS_EC_CHIP_G_UART_BITBANG_H */
