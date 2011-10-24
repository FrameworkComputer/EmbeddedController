/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC, emulated/linux implementation */

#include <stdio.h>
#include <stdarg.h>

#include "ec_uart.h"


static void (*uart_input_callback)(void) = NULL;
static int uart_input_callback_char = -1;
static FILE *uart_stream = NULL;


EcError EcUartInit(void) {
  uart_stream = stdout;
  return EC_SUCCESS;
}


EcError EcUartPrintf(const char* format, ...) {
  va_list args;

  va_start(args, format);
  /* TODO; for now, printf() will be pretty close */
  /* TODO: fixed-sizes for integers won't work because int is 64-bit
   * by default on desktop Linux.  I don't distinguish between %d
   * (int) and %d (int32_t). */
  /* TODO: support for pointers (%p) */
  vfprintf(uart_stream, format, args);
  va_end(args);

  return EC_SUCCESS;
}


EcError EcUartPuts(const char* outstr) {
  fputs(outstr, uart_stream);
  return EC_SUCCESS;
}


void EcUartFlushOutput(void) {
  fflush(uart_stream);
}


void EcUartFlushInput(void) {
  /* TODO */
}


int EcUartPeek(int c) {
  /* TODO */
  return -1;
}


int EcUartGets(char* dest, int size) {
  /* TODO */
  return 0;
}


void EcUartRegisterHasInputCallback(UartHasInputCallback callback, int c) {
  uart_input_callback = callback;
  uart_input_callback_char = c;
}
