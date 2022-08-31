/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>
#include "include/keyboard_mkbp.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

struct keyboard_mkbp_commands_fixture {
	struct ec_mkbp_config config;
};

static void *keyboard_mkbp_setup(void)
{
	static struct keyboard_mkbp_commands_fixture fixture = { 0 };

	return &fixture;
}

static void keyboard_mkbp_before(void *fixture)
{
	struct keyboard_mkbp_commands_fixture *f = fixture;

	get_keyscan_config(&f->config);
}

static void keyboard_mkbp_after(void *fixture)
{
	struct keyboard_mkbp_commands_fixture *f = fixture;
	struct ec_params_mkbp_set_config req = { 0 };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_MKBP_SET_CONFIG, 0, req);

	req.config = f->config;
	host_command_process(&args);
}

ZTEST_SUITE(keyboard_mkbp_commands, drivers_predicate_post_main,
	    keyboard_mkbp_setup, keyboard_mkbp_before, keyboard_mkbp_after,
	    NULL);

/**
 * @brief TestPurpose: Verify EC_CMD_MKBP_GET_CONFIG host command.
 */
ZTEST_USER(keyboard_mkbp_commands, test_mkbp_get_config_cmd)
{
	enum ec_status ret_val;
	struct ec_response_mkbp_get_config resp;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_MKBP_GET_CONFIG, 0, resp);

	ret_val = host_command_process(&args);

	zassert_ok(ret_val, "Expected=%d, returned=%d", EC_SUCCESS, ret_val);
}

/**
 * @brief TestPurpose: Verify EC_CMD_MKBP_SET_CONFIG host command.
 */
ZTEST_USER(keyboard_mkbp_commands, test_mkbp_set_config_cmd)
{
	enum ec_status ret_val;
	struct ec_params_mkbp_set_config req = { 0 };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_MKBP_SET_CONFIG, 0, req);

	get_keyscan_config(&req.config);

	req.config.valid_mask =
		EC_MKBP_VALID_SCAN_PERIOD | EC_MKBP_VALID_POLL_TIMEOUT |
		EC_MKBP_VALID_MIN_POST_SCAN_DELAY |
		EC_MKBP_VALID_OUTPUT_SETTLE | EC_MKBP_VALID_DEBOUNCE_DOWN |
		EC_MKBP_VALID_DEBOUNCE_UP | EC_MKBP_VALID_FIFO_MAX_DEPTH;

	ret_val = host_command_process(&args);

	zassert_ok(ret_val, "Expected=%d, returned=%d", EC_SUCCESS, ret_val);
}
