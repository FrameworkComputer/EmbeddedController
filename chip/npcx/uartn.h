/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UARTN_H
#define __CROS_EC_UARTN_H

#include "common.h"

/*
 * Initialize the UART module.
 */
void uartn_init(uint8_t uart_num);

/*
 * Re-enable the UART transmit interrupt.
 *
 * This also forces triggering a UART interrupt, if the transmit interrupt was
 * disabled.
 */
void uartn_tx_start(uint8_t uart_num);

/* Disable the UART transmit interrupt. */
void uartn_tx_stop(uint8_t uart_num, uint8_t sleep_ena);

/* Flush the transmit FIFO. */
void uartn_tx_flush(uint8_t uart_num);

/* Return non-zero if there is room to transmit a character immediately. */
int uartn_tx_ready(uint8_t uart_num);

/* Return non-zero if a transmit is in progress. */
int uartn_tx_in_progress(uint8_t uart_num);

/* Return non-zero if the UART has a character available to read. */
int uartn_rx_available(uint8_t uart_num);

/*
 * Send a character to the UART data register.
 *
 * If the transmit FIFO is full, blocks until there is space.
 *
 * @param c    Character to send.
 */
void uartn_write_char(uint8_t uart_num, char c);

/*
 * Read one char from the UART data register.
 *
 * @return		The character read.
 */
int uartn_read_char(uint8_t uart_num);

/* Clear all data in the UART Rx FIFO */
void uartn_clear_rx_fifo(int channel);

/* Enable the UART Rx interrupt */
void uartn_rx_int_en(uint8_t uart_num);
/* Enable the UART Wake-up */
void uartn_wui_en(uint8_t uart_num);
/* Enable/disable Tx NXMIP (No Transmit In Progress) interrupt */
void uartn_enable_tx_complete_int(uint8_t uart_num, uint8_t enable);
/*
 * Return true if No Transmit In Progress Interrupt is enabled.
 * Otherwise, return false
 */
int uartn_nxmip_int_is_enable(uint8_t uart_num);
#endif /* __CROS_EC_UARTN_H */
