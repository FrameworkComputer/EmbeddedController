/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "system_boot_time.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

ZTEST_SUITE(host_cmd_get_boot_time, NULL, NULL, NULL, NULL, NULL);

ZTEST(host_cmd_get_boot_time, test_get_boot_time)
{
	int ret;
	struct ec_response_get_boot_time r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_BOOT_TIME, 0, r);

	update_ap_boot_time(ARAIL);
	update_ap_boot_time(RSMRST);
	update_ap_boot_time(ESPIRST);
	update_ap_boot_time(PLTRST_LOW);
	update_ap_boot_time(PLTRST_HIGH);
	update_ap_boot_time(EC_CUR_TIME);
	/* trigger update_ap_boot_time(RESET_CNT) */
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

	ret = host_command_process(&args);

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);

	ccprintf("arail: %" PRIu64 "\n", r.timestamp[ARAIL]);
	ccprintf("rsmrst: %" PRIu64 "\n", r.timestamp[RSMRST]);
	ccprintf("espirst: %" PRIu64 "\n", r.timestamp[ESPIRST]);
	ccprintf("pltrst_low: %" PRIu64 "\n", r.timestamp[PLTRST_LOW]);
	ccprintf("pltrst_high: %" PRIu64 "\n", r.timestamp[PLTRST_HIGH]);
	ccprintf("cnt: %" PRIu16 "\n", r.cnt);
	ccprintf("ec_cur_time: %" PRIu64 "\n", r.timestamp[EC_CUR_TIME]);
}
