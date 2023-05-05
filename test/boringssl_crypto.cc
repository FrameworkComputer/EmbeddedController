/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/elliptic_curve_key.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"
#include "test_util.h"
#include "util.h"

test_static enum ec_error_list test_rand(void)
{
	constexpr uint8_t zero[256] = { 0 };
	uint8_t buf1[256];
	uint8_t buf2[256];

	RAND_bytes(buf1, sizeof(buf1));
	RAND_bytes(buf2, sizeof(buf2));

	TEST_ASSERT_ARRAY_NE(buf1, zero, sizeof(zero));
	TEST_ASSERT_ARRAY_NE(buf2, zero, sizeof(zero));
	TEST_ASSERT_ARRAY_NE(buf1, buf2, sizeof(buf1));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_ecc_keygen(void)
{
	bssl::UniquePtr<EC_KEY> key1 = generate_elliptic_curve_key();

	TEST_NE(key1.get(), nullptr, "%p");

	/* The generated key should be valid.*/
	TEST_EQ(EC_KEY_check_key(key1.get()), 1, "%d");

	bssl::UniquePtr<EC_KEY> key2 = generate_elliptic_curve_key();

	TEST_NE(key2.get(), nullptr, "%p");

	/* The generated key should be valid. */
	TEST_EQ(EC_KEY_check_key(key2.get()), 1, "%d");

	const BIGNUM *priv1 = EC_KEY_get0_private_key(key1.get());
	const BIGNUM *priv2 = EC_KEY_get0_private_key(key2.get());

	/* The generated keys should not be the same. */
	TEST_NE(BN_cmp(priv1, priv2), 0, "%d");

	/* The generated keys should not be zero. */
	TEST_EQ(BN_is_zero(priv1), 0, "%d");
	TEST_EQ(BN_is_zero(priv2), 0, "%d");

	return EC_SUCCESS;
}

extern "C" void run_test(int argc, const char **argv)
{
	RUN_TEST(test_rand);
	RUN_TEST(test_ecc_keygen);
	test_print_result();
}
