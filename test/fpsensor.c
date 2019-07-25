/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_crypto.h"
#include "fpsensor_state.h"
#include "host_command.h"
#include "test_util.h"
#include "util.h"

static const uint8_t fake_rollback_secret[] = {
	0xcf, 0xe3, 0x23, 0x76, 0x35, 0x04, 0xc2, 0x0f,
	0x0d, 0xb6, 0x02, 0xa9, 0x68, 0xba, 0x2a, 0x61,
	0x86, 0x2a, 0x85, 0xd1, 0xca, 0x09, 0x54, 0x8a,
	0x6b, 0xe2, 0xe3, 0x38, 0xde, 0x5d, 0x59, 0x14,
};

static const uint8_t fake_tpm_seed[] = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60,
	0xf8, 0x5a, 0xa0, 0xa6, 0x2c, 0xb3, 0xf5, 0xe2,
	0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78, 0x5c, 0x79,
	0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};

static int rollback_should_fail;

/* Mock the rollback for unit test. */
int rollback_get_secret(uint8_t *secret)
{
	if (rollback_should_fail)
		return EC_ERROR_UNKNOWN;
	memcpy(secret, fake_rollback_secret, sizeof(fake_rollback_secret));
	return EC_SUCCESS;
}

static int check_seed_set_result(const int rv, const uint32_t expected,
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

test_static int test_fp_enc_status_valid_flags(void)
{
	/* Putting expected value here because test_static should take void */
	const uint32_t expected = FP_ENC_STATUS_SEED_SET;
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

test_static int test_derive_encryption_key_failure_seed_not_set(void)
{
	static uint8_t unused_key[SBP_ENC_KEY_LEN];
	static const uint8_t unused_salt[FP_CONTEXT_SALT_BYTES] = { 0 };

	/* GIVEN that the TPM seed is not set. */
	if (fp_tpm_seed_is_set()) {
		ccprintf("%s:%s(): this test should be executed before setting"
			 " TPM seed.\n", __FILE__, __func__);
		return -1;
	}

	/* THEN derivation will fail. */
	TEST_ASSERT(derive_encryption_key(unused_key, unused_salt) ==
		EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;
}

static int test_derive_encryption_key_raw(const uint32_t *user_id_,
					  const uint8_t *salt,
					  const uint8_t *expected_key)
{
	uint8_t key[SBP_ENC_KEY_LEN];
	int rv;

	/*
	 * |user_id| is a global variable used as "info" in HKDF expand
	 * in derive_encryption_key().
	 */
	memcpy(user_id, user_id_, sizeof(user_id));
	rv = derive_encryption_key(key, salt);

	TEST_ASSERT(rv == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(key, expected_key, sizeof(key));

	return EC_SUCCESS;
}

test_static int test_derive_encryption_key(void)
{
	/*
	 * These vectors are obtained by choosing the salt and the user_id
	 * (used as "info" in HKDF), and running boringSSL's HKDF
	 * (https://boringssl.googlesource.com/boringssl/+/c0b4c72b6d4c6f4828a373ec454bd646390017d4/crypto/hkdf/)
	 * locally to get the output key. The IKM used in the run is the
	 * concatenation of |fake_rollback_secret| and |fake_tpm_seed|.
	 */
	static const uint32_t user_id1[] = {
		0x608b1b0b, 0xe10d3d24, 0x0bbbe4e6, 0x807b36d9,
		0x2a1f8abc, 0xea38104a, 0x562d9431, 0x64d721c5,
	};

	static const uint8_t salt1[] = {
		0xd0, 0x88, 0x34, 0x15, 0xc0, 0xfa, 0x8e, 0x22,
		0x9f, 0xb4, 0xd5, 0xa9, 0xee, 0xd3, 0x15, 0x19,
	};

	static const uint8_t key1[] = {
		0xdb, 0x49, 0x6e, 0x1b, 0x67, 0x8a, 0x35, 0xc6,
		0xa0, 0x9d, 0xb6, 0xa0, 0x13, 0xf4, 0x21, 0xb3,
	};

	static const uint32_t user_id2[] = {
		0x2546a2ca, 0xf1891f7a, 0x44aad8b8, 0x0d6aac74,
		0x6a4ab846, 0x9c279796, 0x5a72eae1, 0x8276d2a3,
	};

	static const uint8_t salt2[] = {
		0x72, 0x6b, 0xc1, 0xe4, 0x64, 0xd4, 0xff, 0xa2,
		0x5a, 0xac, 0x5b, 0x0b, 0x06, 0x67, 0xe1, 0x53,
	};

	static const uint8_t key2[] = {
		0x8d, 0x53, 0xaf, 0x4c, 0x96, 0xa2, 0xee, 0x46,
		0x9c, 0xe2, 0xe2, 0x6f, 0xe6, 0x66, 0x3d, 0x3a,
	};

	/*
	 * GIVEN that the TPM seed is set, and reading the rollback secret will
	 * succeed.
	 */
	TEST_ASSERT(fp_tpm_seed_is_set() && !rollback_should_fail);

	/* THEN the derivation will succeed. */
	TEST_ASSERT(test_derive_encryption_key_raw(user_id1, salt1, key1) ==
		EC_SUCCESS);

	TEST_ASSERT(test_derive_encryption_key_raw(user_id2, salt2, key2) ==
		EC_SUCCESS);

	return EC_SUCCESS;
}

test_static int test_derive_encryption_key_failure_rollback_fail(void)
{
	static uint8_t unused_key[SBP_ENC_KEY_LEN];
	static const uint8_t unused_salt[FP_CONTEXT_SALT_BYTES] = { 0 };

	/* GIVEN that reading the rollback secret will fail. */
	rollback_should_fail = 1;
	/* THEN the derivation will fail. */
	TEST_ASSERT(derive_encryption_key(unused_key, unused_salt) ==
		EC_ERROR_HW_INTERNAL);

	/* GIVEN that reading the rollback secret will succeed. */
	rollback_should_fail = 0;
	/* GIVEN that the TPM seed has been set. */
	TEST_ASSERT(fp_tpm_seed_is_set());
	/* THEN the derivation will succeed. */
	TEST_ASSERT(derive_encryption_key(unused_key, unused_salt) ==
		EC_SUCCESS);

	return EC_SUCCESS;
}

test_static int test_fp_tpm_seed_not_set(void)
{
	int rv;
	struct ec_response_fp_encryption_status resp = { 0 };

	/* Initially the seed should not have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0,
				    NULL, 0,
				    &resp, sizeof(resp));

	return check_seed_set_result(rv, 0, &resp);
}

test_static int test_set_fp_tpm_seed(void)
{
	int rv;
	struct ec_params_fp_seed params;
	struct ec_response_fp_encryption_status resp = { 0 };

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	memcpy(params.seed, fake_tpm_seed, sizeof(fake_tpm_seed));

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

	return check_seed_set_result(rv, FP_ENC_STATUS_SEED_SET, &resp);
}

test_static int test_set_fp_tpm_seed_again(void)
{
	int rv;
	struct ec_params_fp_seed params;
	struct ec_response_fp_encryption_status resp = { 0 };

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	params.seed[0] = 0;

	rv = test_send_host_command(EC_CMD_FP_SEED, 0,
					&params, sizeof(params),
					NULL, 0);
	if (rv != EC_RES_ACCESS_DENIED) {
		ccprintf("%s:%s(): rv = %d, setting seed the second time "
		"should result in EC_RES_ACCESS_DENIED but did not.\n",
			__FILE__, __func__, rv);
		return -1;
	}

	/* Now seed should still be set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0,
					NULL, 0,
					&resp, sizeof(resp));

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

void run_test(void)
{
	RUN_TEST(test_fp_enc_status_valid_flags);
	RUN_TEST(test_fp_tpm_seed_not_set);
	RUN_TEST(test_derive_encryption_key_failure_seed_not_set);
	RUN_TEST(test_set_fp_tpm_seed);
	RUN_TEST(test_set_fp_tpm_seed_again);
	RUN_TEST(test_derive_encryption_key);
	RUN_TEST(test_derive_encryption_key_failure_rollback_fail);
	RUN_TEST(test_fp_set_sensor_mode);

	test_print_result();
}
