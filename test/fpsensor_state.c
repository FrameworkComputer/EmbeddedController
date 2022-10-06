/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_state.h"
#include "mock/fpsensor_state_mock.h"
#include "test_util.h"
#include "util.h"

test_static int test_fp_enc_status_valid_flags(void)
{
	/* Putting expected value here because test_static should take void */
	const uint32_t expected = FP_ENC_STATUS_SEED_SET;
	int rv;
	struct ec_response_fp_encryption_status resp = { 0 };

	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				    sizeof(resp));
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): failed to get encryption status. rv = %d\n",
			 __FILE__, __func__, rv);
		return -1;
	}

	if (resp.valid_flags != expected) {
		ccprintf("%s:%s(): expected valid flags 0x%08x, got 0x%08x\n",
			 __FILE__, __func__, expected, resp.valid_flags);
		return -1;
	}

	return EC_RES_SUCCESS;
}

static int
check_seed_set_result(const int rv, const uint32_t expected,
		      const struct ec_response_fp_encryption_status *resp)
{
	const uint32_t actual = resp->status & FP_ENC_STATUS_SEED_SET;

	if (rv != EC_RES_SUCCESS || expected != actual) {
		ccprintf("%s:%s(): rv = %d, seed is set: %d\n", __FILE__,
			 __func__, rv, actual);
		return -1;
	}

	return EC_SUCCESS;
}

test_static int test_fp_tpm_seed_not_set(void)
{
	int rv;
	struct ec_response_fp_encryption_status resp = { 0 };

	/* Initially the seed should not have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				    sizeof(resp));

	return check_seed_set_result(rv, 0, &resp);
}

test_static int test_set_fp_tpm_seed(void)
{
	int rv;
	struct ec_params_fp_seed params;
	struct ec_response_fp_encryption_status resp = { 0 };

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	memcpy(params.seed, default_fake_tpm_seed,
	       sizeof(default_fake_tpm_seed));

	rv = test_send_host_command(EC_CMD_FP_SEED, 0, &params, sizeof(params),
				    NULL, 0);
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d, set seed failed\n", __FILE__,
			 __func__, rv);
		return -1;
	}

	/* Now seed should have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				    sizeof(resp));

	return check_seed_set_result(rv, FP_ENC_STATUS_SEED_SET, &resp);
}

test_static int test_set_fp_tpm_seed_again(void)
{
	int rv;
	struct ec_params_fp_seed params;
	struct ec_response_fp_encryption_status resp = { 0 };

	TEST_ASSERT(fp_tpm_seed_is_set());

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	memcpy(params.seed, default_fake_tpm_seed,
	       sizeof(default_fake_tpm_seed));

	rv = test_send_host_command(EC_CMD_FP_SEED, 0, &params, sizeof(params),
				    NULL, 0);
	if (rv != EC_RES_ACCESS_DENIED) {
		ccprintf("%s:%s(): rv = %d, setting seed the second time "
			 "should result in EC_RES_ACCESS_DENIED but did not.\n",
			 __FILE__, __func__, rv);
		return -1;
	}

	/* Now seed should still be set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				    sizeof(resp));

	return check_seed_set_result(rv, FP_ENC_STATUS_SEED_SET, &resp);
}

test_static int test_fp_set_sensor_mode(void)
{
	uint32_t requested_mode = 0;
	uint32_t output_mode = 0;

	/* Validate initial conditions */
	TEST_ASSERT(FP_MAX_FINGER_COUNT == 5);
	TEST_ASSERT(templ_valid == 0);
	TEST_ASSERT(sensor_mode == 0);

	/* GIVEN missing output parameter, THEN get error */
	TEST_ASSERT(fp_set_sensor_mode(0, NULL) == EC_RES_INVALID_PARAM);
	/* THEN sensor_mode is unchanged */
	TEST_ASSERT(sensor_mode == 0);

	/* GIVEN requested mode includes FP_MODE_DONT_CHANGE, THEN succeed */
	TEST_ASSERT(sensor_mode == 0);
	TEST_ASSERT(output_mode == 0);
	requested_mode = FP_MODE_DONT_CHANGE;
	TEST_ASSERT(fp_set_sensor_mode(requested_mode, &output_mode) ==
		    EC_RES_SUCCESS);
	/* THEN sensor_mode is unchanged */
	TEST_ASSERT(sensor_mode == 0);
	/* THEN output_mode matches sensor_mode */
	TEST_ASSERT(output_mode == sensor_mode);

	/* GIVEN request to change to valid sensor mode */
	TEST_ASSERT(sensor_mode == 0);
	requested_mode = FP_MODE_ENROLL_SESSION;
	/* THEN succeed */
	TEST_ASSERT(fp_set_sensor_mode(requested_mode, &output_mode) ==
		    EC_RES_SUCCESS);
	/* THEN requested mode is returned */
	TEST_ASSERT(requested_mode == output_mode);
	/* THEN sensor_mode is updated */
	TEST_ASSERT(sensor_mode == requested_mode);

	/* GIVEN max number of fingers already enrolled */
	sensor_mode = 0;
	output_mode = 0xdeadbeef;
	templ_valid = FP_MAX_FINGER_COUNT;
	requested_mode = FP_MODE_ENROLL_SESSION;
	/* THEN additional enroll attempt will fail */
	TEST_ASSERT(fp_set_sensor_mode(requested_mode, &output_mode) ==
		    EC_RES_INVALID_PARAM);
	/* THEN output parameters is unchanged */
	TEST_ASSERT(output_mode = 0xdeadbeef);
	/* THEN sensor_mode is unchanged */
	TEST_ASSERT(sensor_mode == 0);

	return EC_SUCCESS;
}

test_static int test_fp_set_maintenance_mode(void)
{
	uint32_t output_mode = 0;

	/* GIVEN request to change to maintenance sensor mode */
	TEST_ASSERT(sensor_mode == 0);
	/* THEN succeed */
	TEST_ASSERT(fp_set_sensor_mode(FP_MODE_SENSOR_MAINTENANCE,
				       &output_mode) == EC_RES_SUCCESS);
	/* THEN requested mode is returned */
	TEST_ASSERT(output_mode == FP_MODE_SENSOR_MAINTENANCE);
	/* THEN sensor_mode is updated */
	TEST_ASSERT(sensor_mode == FP_MODE_SENSOR_MAINTENANCE);

	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_fail_fgr_less_than_zero(void)
{
	/* Create invalid param with fgr < 0 */
	struct ec_params_fp_read_match_secret test_match_secret = {
		.fgr = -1,
	};

	TEST_ASSERT(test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0,
					   &test_match_secret,
					   sizeof(test_match_secret), NULL,
					   0) == EC_RES_INVALID_PARAM);

	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_fail_fgr_large_than_max(void)
{
	/* Create invalid param with fgr = FP_MAX_FINGER_COUNT */
	struct ec_params_fp_read_match_secret test_match_secret = {
		.fgr = FP_MAX_FINGER_COUNT,
	};

	TEST_ASSERT(test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0,
					   &test_match_secret,
					   sizeof(test_match_secret), NULL,
					   0) == EC_RES_INVALID_PARAM);
	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_fail_timeout(void)
{
	/* Create valid param with 0 <= fgr < 5 */
	struct ec_params_fp_read_match_secret test_match_secret_1 = {
		.fgr = 1,
	};

	/* Disable positive secret match to create 0 deadline val */
	fp_disable_positive_match_secret(&positive_match_secret_state);

	TEST_ASSERT(positive_match_secret_state.deadline.val == 0);

	TEST_ASSERT(test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0,
					   &test_match_secret_1,
					   sizeof(test_match_secret_1), NULL,
					   0) == EC_RES_TIMEOUT);

	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_unmatched_fgr(void)
{
	/* Create valid param with 0 <= fgr < 5 */
	uint16_t matched_fgr = 1;
	uint16_t unmatched_fgr = 2;
	struct ec_params_fp_read_match_secret test_match_secret_1 = {
		.fgr = matched_fgr,
	};
	/* Create positive secret match state with valid deadline value,
	 * readable state, and wrong template matched
	 */
	struct positive_match_secret_state test_state = {
		.deadline.val = 5000000,
		.readable = true,
		.template_matched = unmatched_fgr,
	};

	/* Test for the wrong matched finger state */
	positive_match_secret_state = test_state;

	TEST_ASSERT(test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0,
					   &test_match_secret_1,
					   sizeof(test_match_secret_1), NULL,
					   0) == EC_RES_ACCESS_DENIED);

	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_unreadable_state(void)
{
	/* Create valid param with 0 <= fgr < 5 */
	uint16_t matched_fgr = 1;
	struct ec_params_fp_read_match_secret test_match_secret_1 = {
		.fgr = matched_fgr,
	};
	/*
	 * Create positive secret match state with valid deadline value ,
	 * unreadable state, and correct matched template
	 */
	struct positive_match_secret_state test_state = {
		.deadline.val = 5000000,
		.readable = false,
		.template_matched = matched_fgr,
	};

	/* Test for the unreadable state */
	positive_match_secret_state = test_state;

	TEST_ASSERT(test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0,
					   &test_match_secret_1,
					   sizeof(test_match_secret_1), NULL,
					   0) == EC_RES_ACCESS_DENIED);

	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_derive_fail(void)
{
	struct ec_response_fp_read_match_secret response = { 0 };
	/* Create valid param with 0 <= fgr < 5 */
	uint16_t matched_fgr = 1;
	struct ec_params_fp_read_match_secret test_match_secret_1 = {
		.fgr = matched_fgr,
	};
	/* Create positive secret match state with valid deadline value,
	 * readable state, and correct template matched
	 */
	struct positive_match_secret_state test_state_1 = {
		.deadline.val = 5000000,
		.readable = true,
		.template_matched = matched_fgr,
	};
	positive_match_secret_state = test_state_1;
	/* Set fp_positive_match_salt to the trivial value */
	memcpy(fp_positive_match_salt, trivial_fp_positive_match_salt,
	       sizeof(trivial_fp_positive_match_salt));

	/* Test with the correct matched finger state and a trivial
	 * fp_positive_match_salt
	 */
	TEST_ASSERT(test_send_host_command(
			    EC_CMD_FP_READ_MATCH_SECRET, 0,
			    &test_match_secret_1, sizeof(test_match_secret_1),
			    &response, sizeof(response)) == EC_RES_ERROR);
	return EC_SUCCESS;
}

test_static int test_fp_command_read_match_secret_derive_succeed(void)
{
	struct ec_response_fp_read_match_secret response = { 0 };
	/* Create valid param with 0 <= fgr < 5 */
	uint16_t matched_fgr = 1;
	struct ec_params_fp_read_match_secret test_match_secret_1 = {
		.fgr = matched_fgr,
	};

	/* Expected positive_match_secret same as  in test/fpsensor_crypto.c*/
	static const uint8_t
		expected_positive_match_secret_for_empty_user_id[] = {
			0x8d, 0xc4, 0x5b, 0xdf, 0x55, 0x1e, 0xa8, 0x72,
			0xd6, 0xdd, 0xa1, 0x4c, 0xb8, 0xa1, 0x76, 0x2b,
			0xde, 0x38, 0xd5, 0x03, 0xce, 0xe4, 0x74, 0x51,
			0x63, 0x6c, 0x6a, 0x26, 0xa9, 0xb7, 0xfa, 0x68,
		};
	/* Create positive secret match state with valid deadline value,
	 * readable state, and correct template matched
	 */
	struct positive_match_secret_state test_state_1 = {
		.deadline.val = 5000000,
		.readable = true,
		.template_matched = matched_fgr,
	};
	positive_match_secret_state = test_state_1;
	/* Set fp_positive_match_salt to the trivial value */
	memcpy(fp_positive_match_salt, default_fake_fp_positive_match_salt,
	       sizeof(default_fake_fp_positive_match_salt));

	TEST_ASSERT_ARRAY_EQ(
		(uint8_t const *)fp_positive_match_salt,
		(uint8_t const *)default_fake_fp_positive_match_salt,
		sizeof(default_fake_fp_positive_match_salt));

	/* Initialize an empty user_id to compare positive_match_secret */
	memset(user_id, 0, sizeof(user_id));

	TEST_ASSERT(fp_tpm_seed_is_set());
	/* Test with the correct matched finger state and the default fake
	 * fp_positive_match_salt
	 */
	TEST_ASSERT(test_send_host_command(
			    EC_CMD_FP_READ_MATCH_SECRET, 0,
			    &test_match_secret_1, sizeof(test_match_secret_1),
			    &response, sizeof(response)) == EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(
		response.positive_match_secret,
		expected_positive_match_secret_for_empty_user_id,
		sizeof(expected_positive_match_secret_for_empty_user_id));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fp_enc_status_valid_flags);
	RUN_TEST(test_fp_tpm_seed_not_set);
	RUN_TEST(test_set_fp_tpm_seed);
	RUN_TEST(test_set_fp_tpm_seed_again);
	RUN_TEST(test_fp_set_sensor_mode);
	RUN_TEST(test_fp_set_maintenance_mode);
	RUN_TEST(test_fp_command_read_match_secret_fail_fgr_less_than_zero);
	RUN_TEST(test_fp_command_read_match_secret_fail_fgr_large_than_max);
	RUN_TEST(test_fp_command_read_match_secret_fail_timeout);
	RUN_TEST(test_fp_command_read_match_secret_unmatched_fgr);
	RUN_TEST(test_fp_command_read_match_secret_unreadable_state);
	RUN_TEST(test_fp_command_read_match_secret_derive_fail);
	RUN_TEST(test_fp_command_read_match_secret_derive_succeed);
	test_print_result();
}
