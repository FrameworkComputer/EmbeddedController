/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __TEST_TC_UTIL_USER_OVERRIDE_H
#define __TEST_TC_UTIL_USER_OVERRIDE_H

#define PRINT_DATA(fmt, ...) ztest_printk_stdout(fmt, ##__VA_ARGS__)

/**
 * @brief Override printk routine for use by the ZTEST subsystem and send
 * test results directly to STDOUT.
 *
 * This is useful for tests that send their console/shell output to an
 * interface besides the native-posix-uart.
 *
 * Requires CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE=y.
 *
 * @param fmt Printf style format string.
 * @param ... Optional list of format arguments.
 * */
__printf_like(1, 2) void ztest_printk_stdout(const char *fmt, ...);

#endif /* __TEST_TC_UTIL_USER_OVERRIDE_H */