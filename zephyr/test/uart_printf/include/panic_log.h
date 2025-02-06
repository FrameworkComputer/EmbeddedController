/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_UART_PRINTF_INCLUDE_PANIC_LOG_H_
#define ZEPHYR_TEST_UART_PRINTF_INCLUDE_PANIC_LOG_H_

#include <stddef.h>

void panic_log_write_char(const char c);
void panic_log_write_str(const char *str, size_t size);

#endif /* ZEPHYR_TEST_UART_PRINTF_INCLUDE_PANIC_LOG_H_ */