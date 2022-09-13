/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <emul/emul_kb_raw.h>

#include "console.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "keyboard_test_utils.h"
#include "mkbp_info.h"
#include "mkbp_input_devices.h"
#include "test/drivers/test_state.h"

ZTEST(mkbp_info, host_command_mkbp_info__keyboard_info)
{
	/* Get the number of keyboard rows and columns */

	int ret;
	struct ec_response_mkbp_info response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_KBD,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal(KEYBOARD_ROWS, response.rows, NULL);
	zassert_equal(KEYBOARD_COLS_MAX, response.cols, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__supported_buttons)
{
	/* Get the set of supported buttons */

	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_SUPPORTED,
		.event_type = EC_MKBP_EVENT_BUTTON,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal(get_supported_buttons(), response.buttons, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__supported_switches)
{
	/* Get the set of supported switches */

	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_SUPPORTED,
		.event_type = EC_MKBP_EVENT_SWITCH,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal(get_supported_switches(), response.switches, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__supported_invalid)
{
	/* Request support info on a non-existent type of input device. */

	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_SUPPORTED,
		.event_type = EC_MKBP_EVENT_COUNT, /* Unsupported */
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_RES_INVALID_PARAM, ret,
		      "Host command didn't fail properly: %d", ret);
}

ZTEST(mkbp_info, host_command_mkbp_info__current_keyboard_matrix)
{
	/* Hold down a key so we can validate the returned keyboard matrix state
	 */
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(cros_kb_raw));

	emul_kb_raw_set_kbstate(dev, KEYBOARD_ROW_KEY_R, KEYBOARD_COL_KEY_R, 1);
	keyboard_scan_init();

	k_sleep(K_MSEC(100));

	/* Get the current keyboard matrix state */

	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_CURRENT,
		.event_type = EC_MKBP_EVENT_KEY_MATRIX,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);

	zassert_true(response.key_matrix[KEYBOARD_COL_KEY_R] &
			     KEYBOARD_MASK_KEY_R,
		     "Expected key is not pressed");
}

ZTEST(mkbp_info, host_command_mkbp_info__current_host_events)
{
	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_CURRENT,
		.event_type = EC_MKBP_EVENT_HOST_EVENT,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal((uint32_t)host_get_events(), response.host_event, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__current_host_events64)
{
	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_CURRENT,
		.event_type = EC_MKBP_EVENT_HOST_EVENT64,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal(host_get_events(), response.host_event64, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__current_buttons)
{
	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_CURRENT,
		.event_type = EC_MKBP_EVENT_BUTTON,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal(mkbp_get_button_state(), response.buttons, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__current_switches)
{
	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_CURRENT,
		.event_type = EC_MKBP_EVENT_SWITCH,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_SUCCESS, ret, "Host command failed: %d", ret);
	zassert_equal(mkbp_get_switch_state(), response.switches, NULL);
}

ZTEST(mkbp_info, host_command_mkbp_info__current_invalid)
{
	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = EC_MKBP_INFO_CURRENT,
		.event_type = EC_MKBP_EVENT_COUNT, /* Unsupported */
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_RES_INVALID_PARAM, ret, "Host command failed: %d",
		      ret);
}

ZTEST(mkbp_info, host_command_mkbp_info__invalid)
{
	int ret;
	union ec_response_get_next_data response;
	struct ec_params_mkbp_info request = {
		.info_type = -1, /* Unsupported */
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_MKBP_INFO, 0, response, request);

	ret = host_command_process(&args);
	zassert_equal(EC_RES_ERROR, ret, "Host command failed: %d", ret);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	/* Release any pressed keys in the emulator */
	clear_emulated_keys();
}

ZTEST_SUITE(mkbp_info, drivers_predicate_post_main, NULL, reset, reset, NULL);
