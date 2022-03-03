/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "test_mocks.h"

DEFINE_FFF_GLOBALS;

/* Mocks for common/init_rom.c */
DEFINE_FAKE_VALUE_FUNC(const void *, init_rom_map, const void *, int);
DEFINE_FAKE_VOID_FUNC(init_rom_unmap, const void *, int);
DEFINE_FAKE_VALUE_FUNC(int, init_rom_copy, int, int, int);

/**
 * @brief Reset all the fakes before each test.
 */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	RESET_FAKE(init_rom_map);
	RESET_FAKE(init_rom_unmap);
	RESET_FAKE(init_rom_copy);
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);
