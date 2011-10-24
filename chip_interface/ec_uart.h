/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ec_uart.h - UART module for Chrome EC */

#ifndef __CROS_EC_UART_H
#define __CROS_EC_UART_H

#include "ec_common.h"


/* Initializes the UART module. */
EcError EcUartInit(void);

/*****************************************************************************/
/* Output functions
 *
 * Output is buffered.  If the buffer overflows, subsequent output is
 * discarded. */

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
EcError EcUartPrintf(const char* format, ...);

/* Put a null-terminated string to the UART, like puts().
 *
 * Returns error if output was truncated. */
EcError EcUartPuts(const char* outstr);

/* Flushes output.  Blocks until UART has transmitted all output. */
void EcUartFlushOutput(void);

/*****************************************************************************/
/* Input functions
 *
 * Input is buffered.  If the buffer overflows, the oldest input in
 * the buffer is discarded to make room for the new input.
 *
 * Input lines may be terminated by CR ('\r'), LF ('\n'), or CRLF; all
 * are translated to newline. */

/* Flushes input buffer, discarding all input. */
void EcUartFlushInput(void);

/* Non-destructively checks for a character in the input buffer.
 *
 * Returns the offset into the input buffer of character <c>, or -1 if
 * it is not in the input buffer. */
int EcUartPeek(int c);

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
int EcUartGets(char* dest, int size);

/* Callback handler prototype, called when the UART has input. */
typedef void (*UartHasInputCallback)(void);

/* Registers an input callback handler, replacing any existing
 * callback handler.  If callback==NULL, disables callbacks.
 *
 * Callback will be called whenever the UART receives character <c>.
 * If c<0, callback will be called when the UART receives any
 * character. */
void EcUartRegisterHasInputCallback(UartHasInputCallback callback, int c);

/* TODO: getc(), putc() equivalents? */

#endif  /* __CROS_EC_UART_H */
