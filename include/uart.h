/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* uart.h - UART module for Chrome EC */

#ifndef __CROS_EC_UART_H
#define __CROS_EC_UART_H

#include <stdarg.h>  /* For va_list */
#include "common.h"

/**
 * Initialize the UART module.
 */
void uart_init(void);

/**
 * Return non-zero if UART init has completed.
 */
int uart_init_done(void);

/*
 * Output functions
 *
 * Output is buffered.  If the buffer overflows, subsequent output is
 * discarded.
 *
 * Modules should use the output functions in console.h in preference to these
 * routines, so that output can be filtered on a module-by-module basis.
 */

/**
 * Put a single character to the UART, like putchar().
 *
 * @param c		Character to put
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_putc(int c);

/**
 * Put a null-terminated string to the UART, like fputs().
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_puts(const char *outstr);

/**
 * Print formatted output to the UART, like printf().
 *
 * See printf.h for valid formatting codes.
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_printf(const char *format, ...);

/**
 * Print formatted output to the UART, like vprintf().
 *
 * See printf.h for valid formatting codes.
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_vprintf(const char *format, va_list args);

/**
 * Flush output.  Blocks until UART has transmitted all output.
 */
void uart_flush_output(void);

/*
 * Input functions
 *
 * Input is buffered.  If the buffer overflows, the oldest input in
 * the buffer is discarded to make room for the new input.
 *
 * Input lines may be terminated by CR ('\r'), LF ('\n'), or CRLF; all
 * are translated to newline.
 */

/**
 * Read a single character of input, similar to fgetc().
 *
 * @return the character, or -1 if no input waiting.
 */
int uart_getc(void);

/*
 * Hardware UART driver functions
 */

/**
 * Flush the transmit FIFO.
 */
void uart_tx_flush(void);

/**
 * Return non-zero if there is room to transmit a character immediately.
 */
int uart_tx_ready(void);

/**
 * Return non-zero if UART is ready to start a DMA transfer.
 */
int uart_tx_dma_ready(void);

/**
 * Start a UART transmit DMA transfer
 *
 * @param src		Pointer to data to send
 * @param len		Length of transfer in bytes
 */
void uart_tx_dma_start(const char *src, int len);

/**
 * Return non-zero if the UART has a character available to read.
 */
int uart_rx_available(void);

/**
 * Send a character to the UART data register.
 *
 * If the transmit FIFO is full, blocks until there is space.
 *
 * @param c		Character to send.
 */
void uart_write_char(char c);

/**
 * Read one char from the UART data register.
 *
 * @return		The character read.
 */
int uart_read_char(void);

/**
 * Disable all UART related IRQs.
 *
 * Used to avoid concurrent accesses on UART management variables.
 */
void uart_disable_interrupt(void);

/**
 * Re-enable UART IRQs.
 */
void uart_enable_interrupt(void);

/**
 * Re-enable the UART transmit interrupt.
 *
 * This also forces triggering a UART interrupt, if the transmit interrupt was
 * disabled.
 */
void uart_tx_start(void);

/**
 * Disable the UART transmit interrupt.
 */
void uart_tx_stop(void);

/**
 * Helper for processing UART input.
 *
 * Reads the input FIFO until empty.  Intended to be called from the driver
 * interrupt handler.
 */
void uart_process_input(void);

/**
 * Helper for processing UART output.
 *
 * Fills the output FIFO until the transmit buffer is empty or the FIFO full.
 * Intended to be called from the driver interrupt handler.
 */
void uart_process_output(void);

/*
 * COMx functions
 */

/**
 * Enable COMx interrupts
 */
void uart_comx_enable(void);

/**
 * Return non-zero if ok to put a character via uart_comx_putc().
 */
int uart_comx_putc_ok(void);

/**
 * Write a character to the COMx UART interface.
 */
void uart_comx_putc(int c);

#endif  /* __CROS_EC_UART_H */
