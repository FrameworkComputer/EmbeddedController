/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UARTN_H
#define __CROS_EC_UARTN_H

#include "common.h"

/**
 * Initialize the UART module.
 */
void uartn_init(int uart);

/**
 * Flush the transmit FIFO.
 */
void uartn_tx_flush(int uart);

/**
 * Return non-zero if there is room to transmit a character immediately.
 */
int uartn_tx_ready(int uart);

/**
 * Return non-zero if a transmit is in progress.
 */
int uartn_tx_in_progress(int uart);

/*
 * Return non-zero if the UART has a character available to read.
 */
int uartn_rx_available(int uart);

/**
 * Send a character to the UART data register.
 *
 * If the transmit FIFO is full, blocks until there is space.
 *
 * @param c		Character to send.
 */
void uartn_write_char(int uart, char c);

/**
 * Read one char from the UART data register.
 *
 * @return		The character read.
 */
int uartn_read_char(int uart);

/**
 * Disable all UART related IRQs.
 *
 * Used to avoid concurrent accesses on UART management variables.
 */
void uartn_disable_interrupt(int uart);

/**
 * Re-enable UART IRQs.
 */
void uartn_enable_interrupt(int uart);

/**
 * Re-enable the UART transmit interrupt.
 *
 * This also forces triggering a UART interrupt, if the transmit interrupt was
 * disabled.
 */
void uartn_tx_start(int uart);

/**
 * Disable the UART transmit interrupt.
 */
void uartn_tx_stop(int uart);

/* Get UART output status */
int uartn_enabled(int uart);

/* Enable UART output */
void uartn_tx_connect(int uart);

/* Disable UART output */
void uartn_tx_disconnect(int uart);

/* Enable TX and RX. Disable HW flow control and loopback */
void uartn_enable(int uart);

/* Disable TX, RX, HW flow control, and loopback */
void uartn_disable(int uart);
#endif  /* __CROS_EC_UARTN_H */
