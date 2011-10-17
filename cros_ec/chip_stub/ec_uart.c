/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC, empty implementation */

#include "ec_uart.h"


EcError EcUartPrintf(const char* format, ...) {
  return EC_ERROR_UNIMPLEMENTED;
}


EcError EcUartPuts(const char* outstr) {
  return EC_ERROR_UNIMPLEMENTED;
}


void EcUartFlush(void) {
}


void EcUartFlushInput(void) {
}


int EcUartPeek(int c) {
  return -1;
}


int EcUartGets(char* dest, int size) {
  *dest = 0;
  return 0;
}


void EcUartRegisterHasInputCallback(UartHasInputCallback callback, int c) {
}
