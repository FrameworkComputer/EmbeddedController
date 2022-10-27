/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "test_mocks.h"

/* Mocks for common/extpower_gpio.c */
DEFINE_FAKE_VALUE_FUNC(int, extpower_is_present);

/* Mocks for common/system.c */
DEFINE_FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);

/**
 * @brief Reset all the fakes before each test.
 */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	RESET_FAKE(extpower_is_present);
	RESET_FAKE(system_hibernate);
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);
