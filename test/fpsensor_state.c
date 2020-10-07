/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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

void run_test(int argc, char **argv)
{
	RUN_TEST(test_fp_enc_status_valid_flags);
	RUN_TEST(test_fp_tpm_seed_not_set);
	RUN_TEST(test_set_fp_tpm_seed);
	RUN_TEST(test_set_fp_tpm_seed_again);
	RUN_TEST(test_fp_set_sensor_mode);
	RUN_TEST(test_fp_set_maintenance_mode);
	test_print_result();
}
