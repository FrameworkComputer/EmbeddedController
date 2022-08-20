/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test chipset
 */

#define CONFIG_CMD_AP_RESET_LOG

#include "chipset.h"
#include "test_util.h"

static int test_get_shutdown_reason(void)
{
	enum chipset_shutdown_reason reason = chipset_get_shutdown_reason();

	TEST_ASSERT(reason == 0);
	report_ap_reset(CHIPSET_SHUTDOWN_POWERFAIL);
	reason = chipset_get_shutdown_reason();
	TEST_ASSERT(reason == CHIPSET_SHUTDOWN_POWERFAIL);

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_get_shutdown_reason);

	test_print_result();
}
