/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
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

extern "C" void run_test(int argc, const char **argv)
{
	RUN_TEST(test_rand);
	test_print_result();
}
