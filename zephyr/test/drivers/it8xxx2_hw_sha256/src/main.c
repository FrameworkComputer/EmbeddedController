/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <sha256.h>

static const uint8_t sha256_input[] = { 0xaa, 0xaa, 0x55, 0x55 };
static const uint32_t sha256_output_no_caculated[8] = {
	0x67e6096a, 0x85ae67bb, 0x72f36e3c, 0x3af54fa5,
	0x7f520e51, 0x8c68059b, 0xabd9831f, 0x19cde05b
};

ZTEST_SUITE(it8xxx2_hw_sha256_driver, NULL, NULL, NULL, NULL, NULL);

ZTEST(it8xxx2_hw_sha256_driver, test_it8xxx2_hw_sha256)
{
	static struct sha256_ctx ctx __aligned(256);
	uint8_t *hash;
	uint8_t reg_addr, expected_addr;

	SHA256_init(&ctx);
	/* Verify hardware sha256 registers are configured as we want. */
	expected_addr = ((uint32_t)&ctx >> 6) & 0xffc;
	reg_addr = it8xxx2_sha256_get_sha1hbaddr();
	zassert_equal(reg_addr, expected_addr, "sha1hbaddr: 0x%x vs 0x%x",
		      reg_addr, expected_addr);
	expected_addr = ((uint32_t)&ctx.k >> 6) & 0xffc;
	reg_addr = it8xxx2_sha256_get_sha2hbaddr();
	zassert_equal(reg_addr, expected_addr, "sha2hbaddr: 0x%x vs 0x%x",
		      reg_addr, expected_addr);
	SHA256_update(&ctx, sha256_input, sizeof(sha256_input));
	hash = SHA256_final(&ctx);
	/*
	 * Since hash is not actually calculated, the byte order of h0~h7 is
	 * verified here.
	 */
	zassert_mem_equal(hash, sha256_output_no_caculated,
			  sizeof(sha256_output_no_caculated), NULL);
}
