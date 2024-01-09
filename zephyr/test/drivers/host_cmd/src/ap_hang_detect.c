/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_reset_log.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <zephyr/ztest.h>

static void set_timeout(uint16_t timeout)
{
	struct ec_params_hang_detect req = {
		.command = EC_HANG_DETECT_CMD_SET_TIMEOUT,
		.reboot_timeout_sec = timeout
	};
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
}

ZTEST_USER(ap_hang_detect, test_set_parms_good_timeout)
{
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
}

ZTEST_USER(ap_hang_detect, test_set_parms_bad_timeout)
{
	struct ec_params_hang_detect req = {
		.command = EC_HANG_DETECT_CMD_SET_TIMEOUT,
		.reboot_timeout_sec = EC_HANG_DETECT_MIN_TIMEOUT - 1
	};
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	zassert_equal(ec_cmd_hang_detect(&args, &req, &resp),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(ap_hang_detect, test_cancel)
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	/* Confirm the AP booted normally */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);

	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	/* let's wait 1s and then cancel watchdog */
	k_sleep(K_SECONDS(1));
	req.command = EC_HANG_DETECT_CMD_CANCEL;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	/* wait and check if watchdog has rebooted the AP */
	k_sleep(K_SECONDS(30));

	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);
}

static void reload_and_get_status(void)
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	/* Confirm the AP booted noramlly */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);

	/* Set timeout, reload the timer and don't pet the watchdog */
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(2 * EC_HANG_DETECT_MIN_TIMEOUT));

	/* EC should reboot the AP and set accordingly the status */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_EC_WDT);
}

ZTEST_USER(ap_hang_detect, test_reload_and_get_status)
{
	reload_and_get_status();
}

ZTEST_USER(ap_hang_detect, test_clear_status)
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	/* re-use reload_and_get_status() to set
	 * EC_HANG_DETECT_AP_BOOT_EC_WDT status
	 */
	reload_and_get_status();

	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);
}

ZTEST_USER(ap_hang_detect, test_bad_command)
{
	struct ec_params_hang_detect req = {
		/* EC_HANG_DETECT_CMD_CLEAR_STATUS is the last command */
		.command = EC_HANG_DETECT_CMD_CLEAR_STATUS + 1,
	};
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	zassert_equal(ec_cmd_hang_detect(&args, &req, &resp),
		      EC_RES_INVALID_PARAM);
}

ZTEST_SUITE(ap_hang_detect, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
