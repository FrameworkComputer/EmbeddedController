/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "system.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST(hc_system, test_reboot_ec)
{
	int rv;
	struct host_cmd_handler_args args =
		(struct host_cmd_handler_args)BUILD_HOST_COMMAND_SIMPLE(
			EC_CMD_REBOOT, 0);

	RESET_FAKE(system_reset);

	rv = host_command_process(&args);

	/* Expect invalid command after passing the system_reset call, because
	 * the EC_CMD_REBOOT doesn't have actual handler.
	 */
	zassert_equal(EC_RES_INVALID_COMMAND, rv);
	zassert_equal(1, system_reset_fake.call_count);
	zassert_equal(system_reset_fake.arg0_history[0], SYSTEM_RESET_HARD,
		      "Unexpected flags %x", system_reset_fake.arg0_history[0]);
}

ZTEST_SUITE(hc_system, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
