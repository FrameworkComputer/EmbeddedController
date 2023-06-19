/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tests for deprecated EC_CMD_HOST_EVENT_* commands */

#include "include/lpc.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

#define HOST_EVENT_TEST_MASK_VAL EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)

static enum ec_status host_event_set_wake_mask_helper(uint32_t mask)
{
	struct ec_params_host_event_mask params = { .mask = mask };

	return ec_cmd_host_event_set_wake_mask(NULL, &params);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_WAKE_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_wake_mask)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	rv = ec_cmd_host_event_get_wake_mask(NULL, &result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_SET_WAKE_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_wake_mask)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	/* Read the current mask */
	rv = ec_cmd_host_event_get_wake_mask(NULL, &result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
		return;
	}

	/* Default mask is expected to be clear */
	zassert_false(result.mask, "Default host event wake mask is not clear");

	zassert_ok(host_event_set_wake_mask_helper(HOST_EVENT_TEST_MASK_VAL));

	/* Verify the mask changed */
	ec_cmd_host_event_get_wake_mask(NULL, &result);

	zassert_equal(result.mask, HOST_EVENT_TEST_MASK_VAL,
		      "Expected wake mask 0x%08x, returned mask 0x%08x",
		      HOST_EVENT_TEST_MASK_VAL, result.mask);

	/* Clean up the mask */
	zassert_ok(host_event_set_wake_mask_helper(0));
}

static enum ec_status host_event_set_smi_mask_helper(uint32_t mask)
{
	struct ec_params_host_event_mask params = { .mask = mask };

	return ec_cmd_host_event_set_smi_mask(NULL, &params);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_SMI_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_smi_mask)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	rv = ec_cmd_host_event_get_smi_mask(NULL, &result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
		return;
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_SET_SMI_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_smi_mask)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	/* Read the current mask */
	rv = ec_cmd_host_event_get_smi_mask(NULL, &result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
		return;
	}

	/* Default mask is expected to be clear */
	zassert_false(result.mask, "Default host event SMI mask is not clear");

	zassert_ok(host_event_set_smi_mask_helper(HOST_EVENT_TEST_MASK_VAL));

	/* Verify the mask changed */
	zassert_ok(ec_cmd_host_event_get_smi_mask(NULL, &result));

	zassert_equal(result.mask, HOST_EVENT_TEST_MASK_VAL,
		      "Expected SMI mask 0x%08x, returned mask 0x%08x",
		      HOST_EVENT_TEST_MASK_VAL, result.mask);

	/* Clean up the mask */
	zassert_ok(host_event_set_smi_mask_helper(0));
}

static enum ec_status
host_event_get_b_helper(struct ec_response_host_event_mask *r)
{
	return ec_cmd_host_event_get_b(NULL, r);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_B host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_b)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	rv = host_event_get_b_helper(&result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
		return;
	}
}

static enum ec_status
host_event_get_sci_mask_helper(struct ec_response_host_event_mask *r)
{
	return ec_cmd_host_event_get_sci_mask(NULL, r);
}

static enum ec_status host_event_set_sci_mask_helper(uint32_t mask)
{
	struct ec_params_host_event_mask params = { .mask = mask };

	return ec_cmd_host_event_set_sci_mask(NULL, &params);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_GET_SCI_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_sci_mask)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	rv = host_event_get_sci_mask_helper(&result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
		return;
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_SET_SCI_MASK host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_sci_mask)
{
	enum ec_status rv;
	struct ec_response_host_event_mask result = { 0 };

	/* Read the current mask */
	rv = host_event_get_sci_mask_helper(&result);
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		zassert_ok(rv, "Expected %d, returned %d", EC_RES_SUCCESS, rv);
	} else {
		zassert_equal(EC_RES_INVALID_COMMAND, rv,
			      "Expected %d, returned %d",
			      EC_RES_INVALID_COMMAND, rv);
		return;
	}

	/* Default mask is expected to be clear */
	zassert_false(result.mask, "Default host event SCI mask is not clear");

	zassert_ok(host_event_set_sci_mask_helper(HOST_EVENT_TEST_MASK_VAL));

	/* Verify the mask changed */
	zassert_ok(host_event_get_sci_mask_helper(&result));

	zassert_equal(result.mask, HOST_EVENT_TEST_MASK_VAL,
		      "Expected SCI mask 0x%08x, returned mask 0x%08x",
		      HOST_EVENT_TEST_MASK_VAL, result.mask);

	/* Clean up the mask */
	zassert_ok(host_event_set_sci_mask_helper(0));
}
