/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_UART_PRINTF_INCLUDE_PRINTF_H_
#define ZEPHYR_TEST_UART_PRINTF_INCLUDE_PRINTF_H_

#include <zephyr/fff.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*vfnprintf_addchar_t)(void *, int);
DECLARE_FAKE_VALUE_FUNC(int, vfnprintf, vfnprintf_addchar_t, void *,
			const char *, va_list);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_TEST_UART_PRINTF_INCLUDE_PRINTF_H_ */
