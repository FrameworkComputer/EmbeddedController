/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CHIP_G_UART_BITBANG_H
#define __CROS_EC_CHIP_G_UART_BITBANG_H

/* UART Bit Banging */

#include "common.h"
#include "gpio.h"
#include "queue.h"

struct uart_bitbang_properties {
	enum gpio_signal tx_gpio;
	enum gpio_signal rx_gpio;
	uint32_t tx_pinmux_reg;
	uint32_t tx_pinmux_regval;
	uint32_t rx_pinmux_reg;
	uint32_t rx_pinmux_regval;
	struct queue const *uart_in;
	uint32_t baud_rate;
	uint16_t rx_irq;
	uint8_t uart;
	uint8_t parity;
};

/* In order to bitbang a UART, a board must define a bitbang_config. */
extern struct uart_bitbang_properties bitbang_config;

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
 * Sample the RX line on a UART configured for bit banging mode.
 *
 * This is called when a falling edge is seen on the RX line and will attempt to
 * receive a character.  Incoming data with framing errors or parity errors will
 * be discarded.
 */
void uart_bitbang_irq(void);

void uart_bitbang_drain_tx_queue(struct queue const *q);

#endif /* __CROS_EC_CHIP_G_UART_BITBANG_H */
