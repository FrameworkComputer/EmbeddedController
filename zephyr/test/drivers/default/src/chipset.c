/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/ztest.h>

#include "chipset.h"

#include "test/drivers/test_state.h"

ZTEST(chipset, test_get_ap_reset_stats__bad_pointers)
{
	zassert_equal(EC_ERROR_INVAL, get_ap_reset_stats(NULL, 0, NULL));
}

ZTEST(chipset, test_get_ap_reset_stats__happy_path)
{
	struct ap_reset_log_entry reset_log_entries[4];
	uint32_t actual_reset_count, reset_count;

	memset(reset_log_entries, 0, sizeof(reset_log_entries));

	/* Report two AP resets */
	report_ap_reset(CHIPSET_RESET_AP_WATCHDOG);
	report_ap_reset(CHIPSET_RESET_HANG_REBOOT);

	zassert_equal(EC_SUCCESS,
		      get_ap_reset_stats(reset_log_entries,
					 ARRAY_SIZE(reset_log_entries),
					 &reset_count));

	/* Check the reset causes. The reset entry log is not a FIFO, so we get
	 * the last two empty slots followed by the two we triggered above.
	 */
	zassert_equal(0, reset_log_entries[0].reset_cause);
	zassert_equal(0, reset_log_entries[1].reset_cause);
	zassert_equal(CHIPSET_RESET_AP_WATCHDOG,
		      reset_log_entries[2].reset_cause);
	zassert_equal(CHIPSET_RESET_HANG_REBOOT,
		      reset_log_entries[3].reset_cause);

	/* Check reset count */
	actual_reset_count = test_chipset_get_ap_resets_since_ec_boot();
	zassert_equal(actual_reset_count, reset_count,
		      "Found %d resets, expected %d", reset_count,
		      actual_reset_count);
}

static void reset(void *arg)
{
	ARG_UNUSED(arg);

	test_chipset_corrupt_reset_log_checksum();
	init_reset_log();
}

ZTEST_SUITE(chipset, drivers_predicate_post_main, NULL, reset, reset, NULL);
