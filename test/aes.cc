/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
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

#include "benchmark.h"
#include "common.h"
#include "test_util.h"

#include <memory>

extern "C" {
#include "builtin/assert.h"
#include "console.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"
}

#include "aes_gcm_helpers.h"
#include "openssl/aes.h"

/* These must be included after the "openssl/aes.h" */
#include "crypto/fipsmodule/aes/internal.h"
#include "crypto/fipsmodule/modes/internal.h"

/* Temporary buffer, to avoid using too much stack space. */
static uint8_t tmp[512];

struct AesTestVector {
	std::vector<uint8_t> key;
	std::vector<uint8_t> plaintext;
	std::vector<uint8_t> nonce;
	std::vector<uint8_t> ciphertext;
	std::vector<uint8_t> tag;
	/* clang-format off */
	auto operator<=> (const AesTestVector &) const = default;
	/* clang-format on */
};

struct TestVectorHex {
	std::string key;
	std::string plaintext;
	std::string nonce;
	std::string ciphertext;
	std::string tag;
};

test_static std::optional<uint8_t> HexCharToDigit(const char c)
{
	if (c >= '0' && c <= '9')
		return static_cast<uint8_t>(c - '0');

	if (c >= 'a' && c <= 'f')
		return static_cast<uint8_t>(c - 'a' + 10);

	if (c >= 'A' && c <= 'F')
		return static_cast<uint8_t>(c - 'A' + 10);

	return std::nullopt;
}

test_static bool HexStringToBytes(std::string input,
				  std::vector<uint8_t> *output)
{
	size_t count = input.size();
	if ((count % 2) != 0) {
		return false;
	}

	if (output == nullptr) {
		return false;
	}

	output->clear();
	output->reserve(count / 2);
	for (size_t i = 0; i < count; i += 2) {
		// most significant 4 bits
		std::optional<uint8_t> msb = HexCharToDigit(input[i]);
		if (!msb) {
			return false;
		}
		// least significant 4 bits
		std::optional<uint8_t> lsb = HexCharToDigit(input[i + 1]);
		if (!lsb) {
			return false;
		}
		output->emplace_back((*msb << 4) | *lsb);
	}
	return true;
}

test_static ec_error_list TestVectorHexToBytes(const TestVectorHex &input,
					       AesTestVector *output)
{
	TEST_ASSERT(HexStringToBytes(input.key, &output->key));
	TEST_ASSERT(HexStringToBytes(input.plaintext, &output->plaintext));
	TEST_ASSERT(HexStringToBytes(input.nonce, &output->nonce));
	TEST_ASSERT(HexStringToBytes(input.ciphertext, &output->ciphertext));
	TEST_ASSERT(HexStringToBytes(input.tag, &output->tag));
	return EC_SUCCESS;
}

/*
 * Do encryption, put result in |result|, and compare with |ciphertext|.
 */
static int test_aes_gcm_encrypt(uint8_t *result, const uint8_t *key,
				int key_size, const uint8_t *plaintext,
				const uint8_t *ciphertext, int plaintext_size,
				const uint8_t *nonce, int nonce_size,
				const uint8_t *tag, int tag_size)
{
	static AES_KEY aes_key;
	static GCM128_CONTEXT ctx;

	TEST_ASSERT(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
	CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
	TEST_ASSERT(CRYPTO_gcm128_encrypt(&ctx, &aes_key, plaintext, result,
					  plaintext_size));
	TEST_ASSERT(CRYPTO_gcm128_finish(&ctx, tag, tag_size));
	TEST_ASSERT_ARRAY_EQ(ciphertext, result, plaintext_size);

	return EC_SUCCESS;
}

/*
 * Do decryption, put result in |result|, and compare with |plaintext|.
 */
static int test_aes_gcm_decrypt(uint8_t *result, const uint8_t *key,
				int key_size, const uint8_t *plaintext,
				const uint8_t *ciphertext, int plaintext_size,
				const uint8_t *nonce, int nonce_size,
				const uint8_t *tag, int tag_size)
{
	static AES_KEY aes_key;
	static GCM128_CONTEXT ctx;

	TEST_ASSERT(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
	CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
	TEST_ASSERT(CRYPTO_gcm128_decrypt(&ctx, &aes_key, ciphertext, result,
					  plaintext_size));
	TEST_ASSERT(CRYPTO_gcm128_finish(&ctx, tag, tag_size));
	TEST_ASSERT_ARRAY_EQ(plaintext, result, plaintext_size);

	return EC_SUCCESS;
}

static int test_aes_gcm_raw_inplace(const uint8_t *key, int key_size,
				    const uint8_t *plaintext,
				    const uint8_t *ciphertext,
				    int plaintext_size, const uint8_t *nonce,
				    int nonce_size, const uint8_t *tag,
				    int tag_size)
{
	/*
	 * Make copies that will be clobbered during in-place encryption or
	 * decryption.
	 */
	auto plaintext_ptr = std::make_unique<uint8_t[]>(plaintext_size);
	auto ciphertext_ptr = std::make_unique<uint8_t[]>(plaintext_size);
	uint8_t *plaintext_copy = plaintext_ptr.get();
	uint8_t *ciphertext_copy = ciphertext_ptr.get();

	memcpy(plaintext_copy, plaintext, plaintext_size);
	memcpy(ciphertext_copy, ciphertext, plaintext_size);

	TEST_ASSERT(test_aes_gcm_encrypt(plaintext_copy, key, key_size,
					 plaintext_copy, ciphertext,
					 plaintext_size, nonce, nonce_size, tag,
					 tag_size) == EC_SUCCESS);

	TEST_ASSERT(test_aes_gcm_decrypt(ciphertext_copy, key, key_size,
					 plaintext, ciphertext_copy,
					 plaintext_size, nonce, nonce_size, tag,
					 tag_size) == EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_aes_gcm_raw_non_inplace(const uint8_t *key, int key_size,
					const uint8_t *plaintext,
					const uint8_t *ciphertext,
					int plaintext_size,
					const uint8_t *nonce, int nonce_size,
					const uint8_t *tag, int tag_size)
{
	TEST_ASSERT(test_aes_gcm_encrypt(tmp, key, key_size, plaintext,
					 ciphertext, plaintext_size, nonce,
					 nonce_size, tag,
					 tag_size) == EC_SUCCESS);

	TEST_ASSERT(test_aes_gcm_decrypt(tmp, key, key_size, plaintext,
					 ciphertext, plaintext_size, nonce,
					 nonce_size, tag,
					 tag_size) == EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_aes_gcm_raw(const uint8_t *key, int key_size,
			    const uint8_t *plaintext, const uint8_t *ciphertext,
			    std::size_t plaintext_size, const uint8_t *nonce,
			    std::size_t nonce_size, const uint8_t *tag,
			    std::size_t tag_size)
{
	TEST_ASSERT(plaintext_size <= sizeof(tmp));

	TEST_ASSERT(test_aes_gcm_raw_non_inplace(key, key_size, plaintext,
						 ciphertext, plaintext_size,
						 nonce, nonce_size, tag,
						 tag_size) == EC_SUCCESS);
	TEST_ASSERT(test_aes_gcm_raw_inplace(key, key_size, plaintext,
					     ciphertext, plaintext_size, nonce,
					     nonce_size, tag,
					     tag_size) == EC_SUCCESS);

	return EC_SUCCESS;
}

test_static int test_aes_gcm(void)
{
	/*
	 * Test vectors from BoringSSL
	 * https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt
	 * (only the ones with actual data, and no additional data).
	 */

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#8
	TestVectorHex test_vector_hex1 = {
		.key = "00000000000000000000000000000000",
		.plaintext = "00000000000000000000000000000000",
		.nonce = "000000000000000000000000",
		.ciphertext = "0388dace60b6a392f328c2b971b2fe78",
		.tag = "ab6e47d42cec13bdf53a67b21257bddf",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#15
	TestVectorHex test_vector_hex2 = {
		.key = "feffe9928665731c6d6a8f9467308308",
		.plaintext =
			"d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
		.nonce = "cafebabefacedbaddecaf888",
		.ciphertext =
			"42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985",
		.tag = "4d5c2af327cd64a62cf35abd2ba6fab4",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#50
	TestVectorHex test_vector_hex3 = {
		.key = "000000000000000000000000000000000000000000000000",
		.plaintext = "00000000000000000000000000000000",
		.nonce = "000000000000000000000000",
		.ciphertext = "98e7247c07f0fe411c267e4384b0f600",
		.tag = "2ff58d80033927ab8ef4d4587514f0fb",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#57
	TestVectorHex test_vector_hex4 = {
		.key = "feffe9928665731c6d6a8f9467308308feffe9928665731c",
		.plaintext =
			"d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
		.nonce = "cafebabefacedbaddecaf888",
		.ciphertext =
			"3980ca0b3c00e841eb06fac4872a2757859e1ceaa6efd984628593b40ca1e19c7d773d00c144c525ac619d18c84a3f4718e2448b2fe324d9ccda2710acade256",
		.tag = "9924a7c8587336bfb118024db8674a14",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#99
	TestVectorHex test_vector_hex5 = {
		.key = "0000000000000000000000000000000000000000000000000000000000000000",
		.plaintext = "00000000000000000000000000000000",
		.nonce = "000000000000000000000000",
		.ciphertext = "cea7403d4d606b6e074ec5d3baf39d18",
		.tag = "d0d1c8a799996bf0265b98b5d48ab919",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#106
	TestVectorHex test_vector_hex6 = {
		.key = "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
		.plaintext =
			"d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
		.nonce = "cafebabefacedbaddecaf888",
		.ciphertext =
			"522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad",
		.tag = "b094dac5d93471bdec1a502270e3cc6c",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/fipsmodule/modes/gcm_tests.txt#141
	TestVectorHex test_vector_hex7 = {
		.key = "00000000000000000000000000000000",
		.plaintext =
			"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.nonce =
			"ffffffff000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.ciphertext =
			"56b3373ca9ef6e4a2b64fe1e9a17b61425f10d47a75a5fce13efc6bc784af24f4141bdd48cf7c770887afd573cca5418a9aeffcd7c5ceddfc6a78397b9a85b499da558257267caab2ad0b23ca476a53cb17fb41c4b8b475cb4f3f7165094c229c9e8c4dc0a2a5ff1903e501511221376a1cdb8364c5061a20cae74bc4acd76ceb0abc9fd3217ef9f8c90be402ddf6d8697f4f880dff15bfb7a6b28241ec8fe183c2d59e3f9dfff653c7126f0acb9e64211f42bae12af462b1070bef1ab5e3606872ca10dee15b3249b1a1b958f23134c4bccb7d03200bce420a2f8eb66dcf3644d1423c1b5699003c13ecef4bf38a3b60eedc34033bac1902783dc6d89e2e774188a439c7ebcc0672dbda4ddcfb2794613b0be41315ef778708a70ee7d75165c",
		.tag = "8b307f6b33286d0ab026a9ed3fe1e85f",
	};

	const std::array hex_test_vectors = {
		test_vector_hex1, test_vector_hex2, test_vector_hex3,
		test_vector_hex4, test_vector_hex5, test_vector_hex6,
		test_vector_hex7
	};

	std::vector<AesTestVector> test_vectors;
	for (const auto &hex_test_vector : hex_test_vectors) {
		AesTestVector test_vector;
		TEST_ASSERT(TestVectorHexToBytes(hex_test_vector,
						 &test_vector) == EC_SUCCESS);
		test_vectors.emplace_back(test_vector);
	}

	constexpr size_t kExpectedNumTestVectors = 7;
	TEST_EQ(test_vectors.size(), kExpectedNumTestVectors, "%zu");
	for (const auto &test_vector : test_vectors) {
		TEST_ASSERT(!test_aes_gcm_raw(
			test_vector.key.data(), test_vector.key.size(),
			test_vector.plaintext.data(),
			test_vector.ciphertext.data(),
			test_vector.plaintext.size(), test_vector.nonce.data(),
			test_vector.nonce.size(), test_vector.tag.data(),
			test_vector.tag.size()));
	}

	return EC_SUCCESS;
}

static void test_aes_gcm_speed(void)
{
	Benchmark benchmark({ .num_iterations = 1000 });
	static const uint8_t key[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const int key_size = sizeof(key);
	static uint8_t plaintext[512] = { 0 };
	const auto plaintext_size = sizeof(plaintext);
	static const uint8_t nonce[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const int nonce_size = sizeof(nonce);
	uint8_t tag[16] = { 0 };
	const int tag_size = sizeof(tag);

	uint8_t *encrypted_data = tmp;
	static AES_KEY aes_key;
	static GCM128_CONTEXT ctx;

	assert(plaintext_size <= sizeof(tmp));

	benchmark.run("AES-GCM encrypt", [&]() {
		AES_set_encrypt_key(key, 8 * key_size, &aes_key);
		CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
		CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
		CRYPTO_gcm128_encrypt(&ctx, &aes_key, plaintext, encrypted_data,
				      plaintext_size);
		CRYPTO_gcm128_tag(&ctx, tag, tag_size);
	});

	benchmark.run("AES-GCM decrypt", [&]() {
		AES_set_encrypt_key(key, 8 * key_size, &aes_key);
		CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
		CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
		auto decrypt_res =
			CRYPTO_gcm128_decrypt(&ctx, &aes_key, encrypted_data,
					      plaintext, plaintext_size);

		auto finish_res = CRYPTO_gcm128_finish(&ctx, tag, tag_size);
		assert(decrypt_res);
		assert(finish_res);
	});
	benchmark.print_results();
}

static int test_aes_raw(const uint8_t *key, int key_size,
			const uint8_t *plaintext, const uint8_t *ciphertext)
{
	AES_KEY aes_key;
	uint8_t *block = tmp;

	TEST_ASSERT(AES_BLOCK_SIZE <= sizeof(tmp));

	TEST_ASSERT(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test encryption. */
	AES_encrypt(plaintext, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(ciphertext, block, AES_BLOCK_SIZE);

	/* Test in-place encryption. */
	memcpy(block, plaintext, AES_BLOCK_SIZE);
	AES_encrypt(block, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(ciphertext, block, AES_BLOCK_SIZE);

	TEST_ASSERT(AES_set_decrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test decryption. */
	AES_decrypt(ciphertext, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(plaintext, block, AES_BLOCK_SIZE);

	/* Test in-place decryption. */
	memcpy(block, ciphertext, AES_BLOCK_SIZE);
	AES_decrypt(block, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(plaintext, block, AES_BLOCK_SIZE);

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
	Benchmark benchmark({ .num_iterations = 1000 });
	/* Test vectors from FIPS-197, Appendix C. */
	static const uint8_t key[] __aligned(4) = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	const int key_size = sizeof(key);
	static const uint8_t plaintext[] __aligned(4) = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};

	AES_KEY aes_key;
	uint8_t block[AES_BLOCK_SIZE];

	AES_set_encrypt_key(key, 8 * key_size, &aes_key);
	AES_encrypt(plaintext, block, &aes_key);
	benchmark.run("AES", [&block, &aes_key]() {
		AES_encrypt(block, block, &aes_key);
	});
	benchmark.print_results();
}

void run_test(int argc, const char **argv)
{
	watchdog_reload();

	/* do not check result, just as a benchmark */
	test_aes_speed();

	watchdog_reload();
	RUN_TEST(test_aes);

	/* do not check result, just as a benchmark */
	test_aes_gcm_speed();

	watchdog_reload();
	RUN_TEST(test_aes_gcm);

	test_print_result();
}
