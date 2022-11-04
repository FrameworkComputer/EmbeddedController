/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_UART_PRINTF_INCLUDE_UART_H_
#define ZEPHYR_TEST_UART_PRINTF_INCLUDE_UART_H_

#include <zephyr/fff.h>

#ifdef __cplusplus
extern "C" {
#endif

int uart_putc(int c);
int uart_puts(const char *outstr);
int uart_put(const char *out, int len);
int uart_put_raw(const char *out, int len);
int uart_printf(const char *format, ...);

DECLARE_FAKE_VALUE_FUNC(int, uart_tx_char_raw, void *, int);
DECLARE_FAKE_VOID_FUNC(uart_tx_start);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_TEST_UART_PRINTF_INCLUDE_UART_H_ */
