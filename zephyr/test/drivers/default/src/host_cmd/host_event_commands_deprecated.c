/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tests for deprecated EC_CMD_HOST_EVENT_* commands */

#include <zephyr/ztest.h>
#include "include/lpc.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#define HOST_EVENT_TEST_MASK_VAL EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)

static void
host_event_get_wake_mask_helper(struct ec_response_host_event_mask *r)
{
	enum ec_status ret_val;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_HOST_EVENT_GET_WAKE_MASK, 0, *r);

	ret_val = host_command_process(&args);

	/* EC_CMD_HOST_EVENT_GET_WAKE_MASK always returns success */
	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);
}

static void host_event_set_wake_mask_helper(uint32_t mask)
{
	enum ec_status ret_val;
	struct ec_params_host_event_mask params = { .mask = mask };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_HOST_EVENT_SET_WAKE_MASK, 0, params);

	ret_val = host_command_process(&args);

	/* EC_CMD_HOST_EVENT_SET_WAKE_MASK always returns success */
	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_WAKE_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_wake_mask)
{
#ifdef CONFIG_HOSTCMD_X86
	struct ec_response_host_event_mask result = { 0 };

	host_event_get_wake_mask_helper(&result);
#else
	ztest_test_skip();
#endif
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_SET_WAKE_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_wake_mask)
{
#ifdef CONFIG_HOSTCMD_X86
	struct ec_response_host_event_mask result = { 0 };

	/* Read the current mask */
	host_event_get_wake_mask_helper(&result);

	/* Default mask is expected to be clear */
	zassert_false(result.mask, "Default host event wake mask is not clear");

	host_event_set_wake_mask_helper(HOST_EVENT_TEST_MASK_VAL);

	/* Verify the mask changed */
	host_event_get_wake_mask_helper(&result);

	zassert_equal(result.mask, HOST_EVENT_TEST_MASK_VAL,
		      "Expected wake mask 0x%08x, returned mask 0x%08x",
		      HOST_EVENT_TEST_MASK_VAL, result.mask);

	/* Clean up the mask */
	host_event_set_wake_mask_helper(0);
#else
	ztest_test_skip();
#endif
}

static void
host_event_get_smi_mask_helper(struct ec_response_host_event_mask *r)
{
	enum ec_status ret_val;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_HOST_EVENT_GET_SMI_MASK, 0, *r);

	ret_val = host_command_process(&args);

	/* EC_CMD_HOST_EVENT_GET_SMI_MASK always returns success */
	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);
}

static void host_event_set_smi_mask_helper(uint32_t mask)
{
	enum ec_status ret_val;
	struct ec_params_host_event_mask params = { .mask = mask };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_HOST_EVENT_SET_SMI_MASK, 0, params);

	ret_val = host_command_process(&args);

	/* EC_CMD_HOST_EVENT_SET_SMI_MASK always returns success */
	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_SMI_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_smi_mask)
{
#ifdef CONFIG_HOSTCMD_X86
	struct ec_response_host_event_mask result = { 0 };

	host_event_get_smi_mask_helper(&result);
#else
	ztest_test_skip();
#endif
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_SET_SMI_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_smi_mask)
{
#ifdef CONFIG_HOSTCMD_X86
	struct ec_response_host_event_mask result = { 0 };

	/* Read the current mask */
	host_event_get_smi_mask_helper(&result);

	/* Default mask is expected to be clear */
	zassert_false(result.mask, "Default host event SMI mask is not clear");

	host_event_set_smi_mask_helper(HOST_EVENT_TEST_MASK_VAL);

	/* Verify the mask changed */
	host_event_get_smi_mask_helper(&result);

	zassert_equal(result.mask, HOST_EVENT_TEST_MASK_VAL,
		      "Expected SMI mask 0x%08x, returned mask 0x%08x",
		      HOST_EVENT_TEST_MASK_VAL, result.mask);

	/* Clean up the mask */
	host_event_set_smi_mask_helper(0);
#else
	ztest_test_skip();
#endif
}

static void host_event_get_b_helper(struct ec_response_host_event_mask *r)
{
	enum ec_status ret_val;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_HOST_EVENT_GET_B, 0, *r);

	ret_val = host_command_process(&args);

	/* EC_CMD_HOST_EVENT_GET_B always returns success */
	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_B host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_b)
{
#ifdef CONFIG_HOSTCMD_X86
	struct ec_response_host_event_mask result = { 0 };

	host_event_get_b_helper(&result);
#else
	ztest_test_skip();
#endif
}
