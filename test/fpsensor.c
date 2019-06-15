/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_crypto.h"
#include "fpsensor_state.h"
#include "host_command.h"
#include "mock/timer_mock.h"
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

static const uint8_t fake_positive_match_salt[] = {
	0x04, 0x1f, 0x5a, 0xac, 0x5f, 0x79, 0x10, 0xaf,
	0x04, 0x1d, 0x46, 0x3a, 0x5f, 0x08, 0xee, 0xcb,
};

static const uint8_t fake_user_id[] = {
	0x28, 0xb5, 0x5a, 0x55, 0x57, 0x1b, 0x26, 0x88,
	0xce, 0xc5, 0xd1, 0xfe, 0x1d, 0x58, 0x5b, 0x94,
	0x51, 0xa2, 0x60, 0x49, 0x9f, 0xea, 0xb1, 0xea,
	0xf7, 0x04, 0x2f, 0x0b, 0x20, 0xa5, 0x93, 0x64,
};

/*
 * |expected_positive_match_secret_for_empty_user_id| is obtained by running
 * BoringSSL locally.
 * From https://boringssl.googlesource.com/boringssl
 * commit 365b7a0fcbf273b1fa704d151059e419abd6cfb8
 *
 * Steps to reproduce:
 *
 * Open boringssl/crypto/hkdf/hkdf_test.cc
 * Add the following case to static const HKDFTestVector kTests[]
 *
 * // test positive match secret
 * {
 *   EVP_sha256,
 *   {
 *     // IKM:
 *     // fake_rollback_secret
 *     [ ***Copy 32 octets of fake_rollback_secret here*** ]
 *     // fake_tpm_seed
 *     [ ***Copy 32 octets of fake_tpm_seed here*** ]
 *   }, 64,
 *   {
 *     // fake_positive_match_salt
 *     [ ***Copy 16 octets of fake_positive_match_salt here*** ]
 *   }, 16,
 *   {
 *     // Info:
 *     // "positive_match_secret for user "
 *     0x70, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x76, 0x65,
 *     0x5f, 0x6d, 0x61, 0x74, 0x63, 0x68, 0x5f, 0x73,
 *     0x65, 0x63, 0x72, 0x65, 0x74, 0x20, 0x66, 0x6f,
 *     0x72, 0x20, 0x75, 0x73, 0x65, 0x72, 0x20,
 *     // user_id
 *     [ ***Type 32 octets of 0x00 here*** ]
 *   }, 63,
 *   {  // Expected PRK:
 *     0xc2, 0xff, 0x50, 0x2d, 0xb1, 0x7e, 0x87, 0xb1,
 *     0x25, 0x36, 0x3a, 0x88, 0xe1, 0xdb, 0x4f, 0x98,
 *     0x22, 0xb5, 0x66, 0x8c, 0xab, 0xb7, 0xc7, 0x5e,
 *     0xd7, 0x56, 0xbe, 0xde, 0x82, 0x3f, 0xd0, 0x62,
 *   }, 32,
 *   32, { // 32 = L = FP_POSITIVE_MATCH_SECRET_BYTES
 *     // Expected positive match secret:
 *     [ ***Copy 32 octets of expected positive_match_secret here*** ]
 *   }
 * },
 *
 * Then from boringssl/ execute:
 * mkdir build
 * cd build
 * cmake ..
 * make
 * cd ..
 * go run util/all_tests.go
 */
static const uint8_t expected_positive_match_secret_for_empty_user_id[] = {
	0x8d, 0xc4, 0x5b, 0xdf, 0x55, 0x1e, 0xa8, 0x72,
	0xd6, 0xdd, 0xa1, 0x4c, 0xb8, 0xa1, 0x76, 0x2b,
	0xde, 0x38, 0xd5, 0x03, 0xce, 0xe4, 0x74, 0x51,
	0x63, 0x6c, 0x6a, 0x26, 0xa9, 0xb7, 0xfa, 0x68,
};

/*
 * Same as |expected_positive_match_secret_for_empty_user_id| but use
 * |fake_user_id| instead of all-zero user_id.
 */
static const uint8_t expected_positive_match_secret_for_fake_user_id[] = {
	0x0d, 0xf5, 0xac, 0x7c, 0xad, 0x37, 0x0a, 0x66,
	0x2f, 0x71, 0xf6, 0xc6, 0xca, 0x8a, 0x41, 0x69,
	0x8a, 0xd3, 0xcf, 0x0b, 0xc4, 0x5a, 0x5f, 0x4d,
	0x54, 0xeb, 0x7b, 0xad, 0x5d, 0x1b, 0xbe, 0x30,
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

static int test_hkdf_expand_raw(const uint8_t *prk, size_t prk_size,
				const uint8_t *info, size_t info_size,
				const uint8_t *expected_okm, size_t okm_size)
{
	uint8_t actual_okm[okm_size];

	TEST_ASSERT(hkdf_expand(actual_okm, okm_size, prk, prk_size,
		    info, info_size) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(expected_okm, actual_okm, okm_size);
	return EC_SUCCESS;
}

test_static int test_hkdf_expand(void)
{
	/* Test vectors in https://tools.ietf.org/html/rfc5869#appendix-A */
	static const uint8_t prk1[] = {
		0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf,
		0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
		0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31,
		0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5,
	};
	static const uint8_t info1[] = {
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
		0xf8, 0xf9,
	};
	static const uint8_t expected_okm1[] = {
		0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
		0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
		0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
		0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
		0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
		0x58, 0x65,
	};
	static const uint8_t prk2[] = {
		0x06, 0xa6, 0xb8, 0x8c, 0x58, 0x53, 0x36, 0x1a,
		0x06, 0x10, 0x4c, 0x9c, 0xeb, 0x35, 0xb4, 0x5c,
		0xef, 0x76, 0x00, 0x14, 0x90, 0x46, 0x71, 0x01,
		0x4a, 0x19, 0x3f, 0x40, 0xc1, 0x5f, 0xc2, 0x44,
	};
	static const uint8_t info2[] = {
		0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
		0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
		0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
		0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
		0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
		0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
		0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
		0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
	};
	static const uint8_t expected_okm2[] = {
		0xb1, 0x1e, 0x39, 0x8d, 0xc8, 0x03, 0x27, 0xa1,
		0xc8, 0xe7, 0xf7, 0x8c, 0x59, 0x6a, 0x49, 0x34,
		0x4f, 0x01, 0x2e, 0xda, 0x2d, 0x4e, 0xfa, 0xd8,
		0xa0, 0x50, 0xcc, 0x4c, 0x19, 0xaf, 0xa9, 0x7c,
		0x59, 0x04, 0x5a, 0x99, 0xca, 0xc7, 0x82, 0x72,
		0x71, 0xcb, 0x41, 0xc6, 0x5e, 0x59, 0x0e, 0x09,
		0xda, 0x32, 0x75, 0x60, 0x0c, 0x2f, 0x09, 0xb8,
		0x36, 0x77, 0x93, 0xa9, 0xac, 0xa3, 0xdb, 0x71,
		0xcc, 0x30, 0xc5, 0x81, 0x79, 0xec, 0x3e, 0x87,
		0xc1, 0x4c, 0x01, 0xd5, 0xc1, 0xf3, 0x43, 0x4f,
		0x1d, 0x87,
	};
	static const uint8_t prk3[] = {
		0x19, 0xef, 0x24, 0xa3, 0x2c, 0x71, 0x7b, 0x16,
		0x7f, 0x33, 0xa9, 0x1d, 0x6f, 0x64, 0x8b, 0xdf,
		0x96, 0x59, 0x67, 0x76, 0xaf, 0xdb, 0x63, 0x77,
		0xac, 0x43, 0x4c, 0x1c, 0x29, 0x3c, 0xcb, 0x04,
	};
	static const uint8_t expected_okm3[] = {
		0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f,
		0x71, 0x5f, 0x80, 0x2a, 0x06, 0x3c, 0x5a, 0x31,
		0xb8, 0xa1, 0x1f, 0x5c, 0x5e, 0xe1, 0x87, 0x9e,
		0xc3, 0x45, 0x4e, 0x5f, 0x3c, 0x73, 0x8d, 0x2d,
		0x9d, 0x20, 0x13, 0x95, 0xfa, 0xa4, 0xb6, 0x1a,
		0x96, 0xc8,
	};
	static uint8_t unused_output[SHA256_DIGEST_SIZE] = { 0 };

	TEST_ASSERT(test_hkdf_expand_raw(prk1, sizeof(prk1), info1,
					 sizeof(info1), expected_okm1,
					 sizeof(expected_okm1))
		== EC_SUCCESS);
	TEST_ASSERT(test_hkdf_expand_raw(prk2, sizeof(prk2), info2,
					 sizeof(info2), expected_okm2,
					 sizeof(expected_okm2))
		== EC_SUCCESS);
	TEST_ASSERT(test_hkdf_expand_raw(prk3, sizeof(prk3), NULL, 0,
					 expected_okm3, sizeof(expected_okm3))
		== EC_SUCCESS);

	TEST_ASSERT(hkdf_expand(NULL, sizeof(unused_output), prk1,
				sizeof(prk1), info1, sizeof(info1))
		== EC_ERROR_INVAL);
	TEST_ASSERT(hkdf_expand(unused_output, sizeof(unused_output),
				NULL, sizeof(prk1), info1, sizeof(info1))
		== EC_ERROR_INVAL);
	TEST_ASSERT(hkdf_expand(unused_output, sizeof(unused_output),
				prk1, sizeof(prk1), NULL, sizeof(info1))
		== EC_ERROR_INVAL);
	/* Info size too long. */
	TEST_ASSERT(hkdf_expand(unused_output, sizeof(unused_output),
				prk1, sizeof(prk1), info1, 1024)
		== EC_ERROR_INVAL);
	/* OKM size too big. */
	TEST_ASSERT(hkdf_expand(unused_output, 256 * SHA256_DIGEST_SIZE,
				prk1, sizeof(prk1), info1, sizeof(info1))
		== EC_ERROR_INVAL);
	return EC_SUCCESS;
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

	/* Clear state to ensure test independence. */
	memset(user_id, 0, sizeof(user_id));

	return EC_SUCCESS;
}

test_static int test_derive_encryption_key(void)
{
	/*
	 * These vectors are obtained by choosing the salt and the user_id
	 * (used as "info" in HKDF), and running BoringSSL's HKDF
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

test_static int test_derive_new_pos_match_secret(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	/* GIVEN that the encryption salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));
	/*
	 * GIVEN that the TPM seed is set, and reading the rollback secret will
	 * succeed.
	 */
	TEST_ASSERT(fp_tpm_seed_is_set() && !rollback_should_fail);

	/* GIVEN that the salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* THEN the derivation will succeed. */
	TEST_ASSERT(derive_positive_match_secret(output,
						 fake_positive_match_salt)
		== EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(
		output,
		expected_positive_match_secret_for_empty_user_id,
		sizeof(expected_positive_match_secret_for_empty_user_id));

	/* Now change the user_id to be non-trivial. */
	memcpy(user_id, fake_user_id, sizeof(fake_user_id));
	TEST_ASSERT(derive_positive_match_secret(output,
						 fake_positive_match_salt)
		== EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(
		output,
		expected_positive_match_secret_for_fake_user_id,
		sizeof(expected_positive_match_secret_for_fake_user_id));
	memset(user_id, 0, sizeof(user_id));

	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_seed_not_set(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];

	/* GIVEN that seed is not set. */
	TEST_ASSERT(!fp_tpm_seed_is_set());
	/* THEN EVEN IF the encryption salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* Deriving positive match secret will fail. */
	TEST_ASSERT(derive_positive_match_secret(output,
						 fake_positive_match_salt)
		== EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;

}

test_static int test_derive_positive_match_secret_fail_rollback_fail(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];

	/* GIVEN that reading secret from anti-rollback block will fail. */
	rollback_should_fail = 1;
	/* THEN EVEN IF the encryption salt is not trivial. */
	TEST_ASSERT(!bytes_are_trivial(fake_positive_match_salt,
				       sizeof(fake_positive_match_salt)));

	/* Deriving positive match secret will fail. */
	TEST_ASSERT(derive_positive_match_secret(output,
						 fake_positive_match_salt)
		== EC_ERROR_HW_INTERNAL);
	rollback_should_fail = 0;

	return EC_SUCCESS;
}

test_static int test_derive_positive_match_secret_fail_salt_trivial(void)
{
	static uint8_t output[FP_POSITIVE_MATCH_SECRET_BYTES];
	/* GIVEN that the salt is trivial. */
	static const uint8_t salt[FP_CONTEXT_SALT_BYTES] = { 0 };

	/* THEN deriving positive match secret will fail. */
	TEST_ASSERT(derive_positive_match_secret(output, salt)
		== EC_ERROR_INVAL);
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

	/* Clear state to ensure test independence. */
	templ_valid = 0;

	return EC_SUCCESS;
}

test_static int test_enable_positive_match_secret(void)
{
	struct positive_match_secret_state dumb_state = {
		.template_matched = FP_NO_SUCH_TEMPLATE,
		.readable = false,
		.deadline.val = 0,
	};
	timestamp_t now = get_time();

	TEST_ASSERT(fp_enable_positive_match_secret(0, &dumb_state) ==
		EC_SUCCESS);
	TEST_ASSERT(dumb_state.template_matched == 0);
	TEST_ASSERT(dumb_state.readable == true);
	TEST_ASSERT(dumb_state.deadline.val == now.val + (5 * SECOND));

	/* Trying to enable again before reading secret should fail. */
	TEST_ASSERT(fp_enable_positive_match_secret(0, &dumb_state) ==
		EC_ERROR_UNKNOWN);
	TEST_ASSERT(dumb_state.template_matched == FP_NO_SUCH_TEMPLATE);
	TEST_ASSERT(dumb_state.readable == false);
	TEST_ASSERT(dumb_state.deadline.val == 0);

	return EC_SUCCESS;
}

test_static int test_disable_positive_match_secret(void)
{
	struct positive_match_secret_state dumb_state;

	TEST_ASSERT(fp_enable_positive_match_secret(0, &dumb_state) ==
		EC_SUCCESS);
	fp_disable_positive_match_secret(&dumb_state);
	TEST_ASSERT(dumb_state.template_matched == FP_NO_SUCH_TEMPLATE);
	TEST_ASSERT(dumb_state.readable == false);
	TEST_ASSERT(dumb_state.deadline.val == 0);

	return EC_SUCCESS;
}

test_static int test_command_read_match_secret(void)
{
	int rv;
	struct ec_params_fp_read_match_secret params;
	struct ec_response_fp_read_match_secret resp;
	timestamp_t now = get_time();

	/* Invalid finger index should be rejected. */
	params.fgr = FP_NO_SUCH_TEMPLATE;
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);
	params.fgr = FP_MAX_FINGER_COUNT;
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	memset(&resp, 0, sizeof(resp));
	/* GIVEN that finger index is valid. */
	params.fgr = 0;

	/* GIVEN that positive match secret is enabled. */
	fp_enable_positive_match_secret(params.fgr,
					&positive_match_secret_state);

	/* GIVEN that salt is non-trivial. */
	memcpy(fp_positive_match_salt[0], fake_positive_match_salt,
	       sizeof(fp_positive_match_salt[0]));
	/* THEN reading positive match secret should succeed. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d\n", __FILE__, __func__, rv);
		return -1;
	}
	/* AND the readable bit should be cleared after the read. */
	TEST_ASSERT(positive_match_secret_state.readable == false);

	TEST_ASSERT_ARRAY_EQ(
		resp.positive_match_secret,
		expected_positive_match_secret_for_empty_user_id,
		sizeof(expected_positive_match_secret_for_empty_user_id));

	/*
	 * Now try reading secret again.
	 * EVEN IF the deadline has not passed.
	 */
	positive_match_secret_state.deadline.val = now.val + 1 * SECOND;
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	/*
	 * This time the command should fail because the
	 * fp_pos_match_secret_readable bit is cleared when the secret was read
	 * the first time.
	 */
	TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);

	return EC_SUCCESS;
}

test_static int test_command_read_match_secret_wrong_finger(void)
{
	int rv;
	struct ec_params_fp_read_match_secret params;

	/* GIVEN that the finger is not the matched or enrolled finger. */
	params.fgr = 0;
	/*
	 * GIVEN that positive match secret is enabled for a different
	 * finger.
	 */
	fp_enable_positive_match_secret(params.fgr + 1,
					&positive_match_secret_state);

	/* Reading secret will fail. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);
	return EC_SUCCESS;
}

test_static int test_command_read_match_secret_timeout(void)
{
	int rv;
	struct ec_params_fp_read_match_secret params;

	params.fgr = 0;
	/* GIVEN that the read is too late. */
	fp_enable_positive_match_secret(params.fgr,
					&positive_match_secret_state);
	set_time(positive_match_secret_state.deadline);

	/* EVEN IF encryption salt is non-trivial. */
	memcpy(fp_positive_match_salt[0], fake_positive_match_salt,
	       sizeof(fp_positive_match_salt[0]));
	/* Reading secret will fail. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_TIMEOUT);
	return EC_SUCCESS;
}

test_static int test_command_read_match_secret_unreadable(void)
{
	int rv;
	struct ec_params_fp_read_match_secret params;

	params.fgr = 0;
	/* GIVEN that the readable bit is not set. */
	fp_enable_positive_match_secret(params.fgr,
					&positive_match_secret_state);
	positive_match_secret_state.readable = false;

	/* EVEN IF the finger is just matched. */
	TEST_ASSERT(positive_match_secret_state.template_matched
		== params.fgr);

	/* EVEN IF encryption salt is non-trivial. */
	memcpy(fp_positive_match_salt[0], fake_positive_match_salt,
	       sizeof(fp_positive_match_salt[0]));
	/* Reading secret will fail. */
	rv = test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET, 0, &params,
				    sizeof(params), NULL, 0);
	TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);
	return EC_SUCCESS;
}

void run_test(void)
{
	/* These are independent of global state. */
	RUN_TEST(test_hkdf_expand);
	RUN_TEST(test_fp_set_sensor_mode);

	/* These must be run before tpm seed is set. */
	RUN_TEST(test_fp_enc_status_valid_flags);
	RUN_TEST(test_fp_tpm_seed_not_set);
	RUN_TEST(test_derive_encryption_key_failure_seed_not_set);
	RUN_TEST(test_derive_positive_match_secret_fail_seed_not_set);

	RUN_TEST(test_set_fp_tpm_seed);

	/* These must be run after tpm seed is set. */
	RUN_TEST(test_set_fp_tpm_seed_again);
	RUN_TEST(test_derive_encryption_key);
	RUN_TEST(test_derive_encryption_key_failure_rollback_fail);
	RUN_TEST(test_derive_new_pos_match_secret);
	RUN_TEST(test_derive_positive_match_secret_fail_rollback_fail);
	RUN_TEST(test_derive_positive_match_secret_fail_salt_trivial);
	RUN_TEST(test_enable_positive_match_secret);
	RUN_TEST(test_disable_positive_match_secret);
	RUN_TEST(test_command_read_match_secret);
	RUN_TEST(test_command_read_match_secret_wrong_finger);
	RUN_TEST(test_command_read_match_secret_timeout);
	RUN_TEST(test_command_read_match_secret_unreadable);

	test_print_result();
}
