/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "printf.h"
#include "uart.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

/* printf.h */
DEFINE_FAKE_VALUE_FUNC(int, vfnprintf, vfnprintf_addchar_t, void *,
		       const char *, va_list);

/* uart.h */
DEFINE_FAKE_VALUE_FUNC(int, uart_tx_char_raw, void *, int);
DEFINE_FAKE_VOID_FUNC(uart_tx_start);

static void fake_reset_rule_before(const struct ztest_unit_test *test,
				   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	/* printf.h */
	RESET_FAKE(vfnprintf);

	/* uart.h */
	RESET_FAKE(uart_tx_char_raw);
	RESET_FAKE(uart_tx_start);
}

ZTEST_RULE(fake_reset, fake_reset_rule_before, nullptr);
