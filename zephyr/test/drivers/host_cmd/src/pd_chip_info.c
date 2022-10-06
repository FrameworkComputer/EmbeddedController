/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#define TEST_PORT USBC_PORT_C0
#define BAD_PORT 65

static enum ec_status run_pd_chip_info(int port,
				       struct ec_response_pd_chip_info_v1 *resp)
{
	struct ec_params_pd_chip_info params = { .port = port, .live = true };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_PD_CHIP_INFO, 1, resp, params);

	return host_command_process(&args);
}

ZTEST_USER(host_cmd_pd_chip_info, test_good_index)
{
	struct ec_response_pd_chip_info_v1 response;

	zassert_ok(run_pd_chip_info(TEST_PORT, &response),
		   "Failed to process pd_get_chip_info for port %d", TEST_PORT);
	/*
	 * Note: verification of the specific fields depends on the chips used
	 * and therefore would belong in a driver-level test
	 */
}

ZTEST_USER(host_cmd_pd_chip_info, test_bad_index)
{
	struct ec_response_pd_chip_info_v1 response;

	zassume_true(board_get_usb_pd_port_count() < BAD_PORT,
		     "Intended bad port exists");
	zassert_equal(run_pd_chip_info(BAD_PORT, &response),
		      EC_RES_INVALID_PARAM,
		      "Failed to fail pd_chip_info for port %d", BAD_PORT);
}

static void host_cmd_pd_chip_info_begin(void *data)
{
	ARG_UNUSED(data);

	/* Assume we have at least one USB-C port */
	zassume_true(board_get_usb_pd_port_count() > 0,
		     "Insufficient TCPCs found");

	/* Set the system into S0, since the AP would drive these commands */
	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));
}

ZTEST_SUITE(host_cmd_pd_chip_info, drivers_predicate_post_main, NULL,
	    host_cmd_pd_chip_info_begin, NULL, NULL);
