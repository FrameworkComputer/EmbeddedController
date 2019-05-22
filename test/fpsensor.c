/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_state.h"
#include "host_command.h"
#include "test_util.h"
#include "util.h"

static int check_fp_enc_status_valid_flags(const uint32_t expected)
{
	int rv;
	struct ec_response_fp_encryption_status resp = { 0 };

	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0,
				    NULL, 0,
				    &resp, sizeof(resp));
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

static int check_fp_tpm_seed_not_set(void)
{
	int rv;
	struct ec_response_fp_encryption_status resp = { 0 };

	/* Initially the seed should not have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0,
				    NULL, 0,
				    &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS || resp.status & FP_ENC_STATUS_SEED_SET) {
		ccprintf("%s:%s(): rv = %d, seed is set: %d\n", __FILE__,
			 __func__, rv, resp.status & FP_ENC_STATUS_SEED_SET);
		return -1;
	}

	return EC_RES_SUCCESS;
}

static int set_fp_tpm_seed(void)
{
	/*
	 * TODO(yichengli): test setting the seed twice:
	 * the second time fails;
	 * the seed is still set.
	 */
	int rv;
	struct ec_params_fp_seed params;
	struct ec_response_fp_encryption_status resp = { 0 };

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	params.seed[0] = 0;

	rv = test_send_host_command(EC_CMD_FP_SEED, 0,
				    &params, sizeof(params),
				    NULL, 0);
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d, set seed failed\n",
			 __FILE__, __func__, rv);
		return -1;
	}

	/* Now seed should have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0,
				    NULL, 0,
				    &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS || !(resp.status & FP_ENC_STATUS_SEED_SET)) {
		ccprintf("%s:%s(): rv = %d, seed is set: %d\n", __FILE__,
			 __func__, rv, resp.status & FP_ENC_STATUS_SEED_SET);
		return -1;
	}

	return EC_RES_SUCCESS;
}

test_static int test_fpsensor(void)
{
	TEST_ASSERT(check_fp_enc_status_valid_flags(FP_ENC_STATUS_SEED_SET) ==
		    EC_RES_SUCCESS);
	TEST_ASSERT(check_fp_tpm_seed_not_set() == EC_RES_SUCCESS);
	TEST_ASSERT(set_fp_tpm_seed() == EC_RES_SUCCESS);

	return EC_SUCCESS;
}

void run_test(void)
{
	RUN_TEST(test_fpsensor);

	test_print_result();
}
