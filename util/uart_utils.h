/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_UART_UTILS_H
#define __UTIL_UART_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Discards (flushes) any data received via UART, but not yet retrieved.
 *
 * @param fd File descriptor of the UART device.
 * @return Number of bytes discarded.
 */
int uart_flush(int fd);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_UART_UTILS_H */
