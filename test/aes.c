/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include "aes.h"
#include "console.h"
#include "common.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

static int test_aes_raw(const uint8_t *key, int key_size,
			const uint8_t *plaintext, const uint8_t *ciphertext)
{
	AES_KEY aes_key;
	uint8_t block[AES_BLOCK_SIZE];

	TEST_ASSERT(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test encryption. */
	AES_encrypt(plaintext, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(ciphertext, block, sizeof(block));

	/* Test in-place encryption. */
	memcpy(block, plaintext, AES_BLOCK_SIZE);
	AES_encrypt(block, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(ciphertext, block, sizeof(block));

	TEST_ASSERT(AES_set_decrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test decryption. */
	AES_decrypt(ciphertext, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(plaintext, block, sizeof(block));

	/* Test in-place decryption. */
	memcpy(block, ciphertext, AES_BLOCK_SIZE);
	AES_decrypt(block, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(plaintext, block, sizeof(block));

	return EC_SUCCESS;
}

static int test_aes(void)
{
	/* Test vectors from FIPS-197, Appendix C. */
	static const uint8_t key1[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	static const uint8_t plain1[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};
	static const uint8_t cipher1[] = {
		0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
		0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
	};

	static const uint8_t key2[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	};
	static const uint8_t plain2[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};
	static const uint8_t cipher2[] = {
		0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c, 0xdf, 0xe0,
		0x6e, 0xaf, 0x70, 0xa0, 0xec, 0x0d, 0x71, 0x91,
	};

	static const uint8_t key3[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	};
	static const uint8_t plain3[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};
	static const uint8_t cipher3[] = {
		0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
		0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89,
	};

	TEST_ASSERT(!test_aes_raw(key1, sizeof(key1), plain1, cipher1));
	TEST_ASSERT(!test_aes_raw(key2, sizeof(key2), plain2, cipher2));
	TEST_ASSERT(!test_aes_raw(key3, sizeof(key3), plain3, cipher3));

	return EC_SUCCESS;
}

static void test_aes_speed(void)
{
	int i;
	/* Test vectors from FIPS-197, Appendix C. */
	static const uint8_t key[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	const int key_size = sizeof(key);
	static const uint8_t plaintext[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};

	AES_KEY aes_key;
	uint8_t block[AES_BLOCK_SIZE];
	timestamp_t t0, t1;

	AES_set_encrypt_key(key, 8 * key_size, &aes_key);
	AES_encrypt(plaintext, block, &aes_key);
	t0 = get_time();
	for (i = 0; i < 1000; i++)
		AES_encrypt(block, block, &aes_key);
	t1 = get_time();
	ccprintf("AES duration %ld us\n", t1.val - t0.val);
}

void run_test(void)
{
	watchdog_reload();

	/* do not check result, just as a benchmark */
	test_aes_speed();

	watchdog_reload();
	RUN_TEST(test_aes);

	test_print_result();
}
