/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

LOG_MODULE_REGISTER(test);

ZTEST_SUITE(system, NULL, NULL, NULL, NULL, NULL);

ZTEST(system, test_chipset_in_state)
{
	zassert_equal(CHIPSET_STATE_ON, chipset_in_state(CHIPSET_STATE_ON));
	zassert_equal(0, chipset_in_state(CHIPSET_STATE_SUSPEND));
}

ZTEST(system, test_bbram_get)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, NULL));
}

ZTEST(system, test_bbram_set)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      system_set_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, 0));
}

ZTEST(system, test_save_read_chip_reset_flags)
{
	chip_save_reset_flags(0);
	zassert_equal(chip_read_reset_flags(), EC_RESET_FLAG_POWER_ON);
}

ZTEST(system, test_set_get_scratchpad)
{
	uint32_t set_value = 0x1234;
	uint32_t read_value;

	zassert_equal(EC_ERROR_UNIMPLEMENTED, system_set_scratchpad(set_value));
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      system_get_scratchpad(&read_value));
}

ZTEST(system, test_ish_system_get_chip_values)
{
	/* Vendor */
	zassert_mem_equal(system_get_chip_vendor(), "Intel", sizeof("Intel"));

	/* Name */
	zassert_mem_equal(system_get_chip_name(), "Intel x86",
			  sizeof("Intel x86"));

	/* Revision */
	zassert_mem_equal(system_get_chip_revision(), "", sizeof(""));
}
