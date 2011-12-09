/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* uart.h - UART module for Chrome EC */

#ifndef __CROS_EC_UART_H
#define __CROS_EC_UART_H

#include "common.h"


/* Initializes the UART module. */
int uart_init(void);


/* Enables console mode if <enable>!=0.  In console mode:
 *    - Input is echoed
 *    - Input CRLF and CR are translated to LF
 *    - Input backspace will remove characters from the input buffer (which
 *      is pretty much only useful if the input handler is only triggered on
 *      newline)
 *    - Output LF is translated to CRLF */
void uart_set_console_mode(int enable);

/*****************************************************************************/
/* Output functions
 *
 * Output is buffered.  If the buffer overflows, subsequent output is
 * discarded. */

/* Put a null-terminated string to the UART, like fputs().
 *
 * Returns error if output was truncated. */
int uart_puts(const char *outstr);

/* Print formatted output to the UART, like printf().
 *
 * Returns error if output was truncated.
 *
 * Must support format strings for:
 *   char (%c)
 *   string (%s)
 *   native int (signed/unsigned) (%d / %u / %x)
 *   int32_t / uint32_t (%d / %x)
 *   int64_t / uint64_t (%ld / %lu / %lx)
 *   pointer (%p)
 * including padding (%-5s, %8d, %08x)
 *
 * Note: Floating point output (%f / %g) is not required.
 */
int uart_printf(const char *format, ...);

/* Flushes output.  Blocks until UART has transmitted all output. */
void uart_flush_output(void);

/* Flushes output.
 *
 * Blocks until UART has transmitted all output,
 * even if we are in high priority interrupt context
 */
void uart_emergency_flush(void);

/*****************************************************************************/
/* Input functions
 *
 * Input is buffered.  If the buffer overflows, the oldest input in
 * the buffer is discarded to make room for the new input.
 *
 * Input lines may be terminated by CR ('\r'), LF ('\n'), or CRLF; all
 * are translated to newline. */

/* Flushes input buffer, discarding all input. */
void uart_flush_input(void);

/* Non-destructively checks for a character in the input buffer.
 *
 * Returns the offset into the input buffer of character <c>, or -1 if
 * it is not in the input buffer. */
int uart_peek(int c);

/* Reads a single character of input, similar to fgetc().  Returns the
 * character, or -1 if no input waiting. */
int uart_getc(void);

/* Reads characters from the UART, similar to fgets().
 *
 * Reads input until one of the following conditions is met:
 *    (1)  <size-1> characters have been read.
 *    (2)  A newline ('\n') has been read.
 *    (3)  The input buffer is empty.
 *
 * Condition (3) means this call never blocks.  This is important
 * because it prevents a race condition where the caller calls
 * UartPeek() to see if input is waiting, or is notified by the
 * callack that input is waiting, but then the input buffer overflows
 * or someone else grabs the input before UartGets() is called.
 *
 * Characters are stored in <dest> and are null-terminated.
 * Characters include the newline if present, so that the caller can
 * distinguish between a complete line and a truncated one.  If the
 * input buffer is empty, a null-terminated empty string ("") is
 * returned.
 *
 * Returns the number of characters read (not counting the terminating
 * null). */
int uart_gets(char *dest, int size);

/* TODO: getc(), putc() equivalents? */

/*****************************************************************************/
/* COMx functions */

/* Returns non-zero if ok to put a character via uart_comx_putc(). */
int uart_comx_putc_ok(void);

/* Puts a character to the COMx interface. */
void uart_comx_putc(int c);

#endif  /* __CROS_EC_UART_H */
