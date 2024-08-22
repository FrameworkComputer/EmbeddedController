/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "benchmark.h"
#include "openssl/aead.h"
#include "openssl/aes.h"
#include "openssl/cipher.h"
#include "openssl/err.h"

#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include <memory>
#include <span>

ZTEST_SUITE(aes, NULL, NULL, NULL, NULL, NULL);

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

/* Temporary buffer, to avoid using too much stack space. */
static uint8_t tmp[512];

static std::optional<uint8_t> HexCharToDigit(const char c)
{
	if (c >= '0' && c <= '9')
		return static_cast<uint8_t>(c - '0');

	if (c >= 'a' && c <= 'f')
		return static_cast<uint8_t>(c - 'a' + 10);

	if (c >= 'A' && c <= 'F')
		return static_cast<uint8_t>(c - 'A' + 10);

	return std::nullopt;
}

static bool HexStringToBytes(std::string input, std::vector<uint8_t> *output)
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

static void TestVectorHexToBytes(const TestVectorHex &input,
				 AesTestVector *output)
{
	zassert_true(HexStringToBytes(input.key, &output->key));
	zassert_true(HexStringToBytes(input.plaintext, &output->plaintext));
	zassert_true(HexStringToBytes(input.nonce, &output->nonce));
	zassert_true(HexStringToBytes(input.ciphertext, &output->ciphertext));
	zassert_true(HexStringToBytes(input.tag, &output->tag));
}

/*
 * Do encryption, put result in |result|, and compare with |ciphertext|.
 */
static void test_aes_gcm_encrypt(uint8_t *result, const uint8_t *key,
				 int key_size, const uint8_t *plaintext,
				 const uint8_t *ciphertext, int plaintext_size,
				 const uint8_t *nonce, int nonce_size,
				 const uint8_t *tag, int tag_size)
{
	bssl::ScopedEVP_AEAD_CTX ctx;

	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(), key,
				    key_size, tag_size, nullptr);
	zassert_true(ret == 1);

	std::vector<uint8_t> out_tag(tag_size);
	size_t out_tag_len = 0;

	const std::span<uint8_t> extra_input; /* no extra input */
	const std::span<uint8_t> additional_data; /* no additional data */

	ret = EVP_AEAD_CTX_seal_scatter(ctx.get(), result, out_tag.data(),
					&out_tag_len, tag_size, nonce,
					nonce_size, plaintext, plaintext_size,
					extra_input.data(), extra_input.size(),
					additional_data.data(),
					additional_data.size());
	zassert_true(ret == 1);
	zassert_true(out_tag_len == static_cast<size_t>(tag_size));

	zassert_mem_equal(tag, out_tag.data(), tag_size);
	zassert_mem_equal(ciphertext, result, plaintext_size);
}

/*
 * Do decryption, put result in |result|, and compare with |plaintext|.
 */
static void test_aes_gcm_decrypt(uint8_t *result, const uint8_t *key,
				 int key_size, const uint8_t *plaintext,
				 const uint8_t *ciphertext, int plaintext_size,
				 const uint8_t *nonce, int nonce_size,
				 const uint8_t *tag, int tag_size)
{
	bssl::ScopedEVP_AEAD_CTX ctx;

	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(), key,
				    key_size, tag_size, nullptr);
	zassert_true(ret == 1);

	std::span<uint8_t> additional_data; /* no additional data */
	ret = EVP_AEAD_CTX_open_gather(ctx.get(), result, nonce, nonce_size,
				       ciphertext, plaintext_size, tag,
				       tag_size, additional_data.data(),
				       additional_data.size());
	zassert_true(ret == 1);

	zassert_mem_equal(plaintext, result, plaintext_size);
}

static void test_aes_gcm_raw_inplace(const uint8_t *key, int key_size,
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

	test_aes_gcm_encrypt(plaintext_copy, key, key_size, plaintext_copy,
			     ciphertext, plaintext_size, nonce, nonce_size, tag,
			     tag_size);

	test_aes_gcm_decrypt(ciphertext_copy, key, key_size, plaintext,
			     ciphertext_copy, plaintext_size, nonce, nonce_size,
			     tag, tag_size);
}

static void test_aes_gcm_raw_non_inplace(const uint8_t *key, int key_size,
					 const uint8_t *plaintext,
					 const uint8_t *ciphertext,
					 int plaintext_size,
					 const uint8_t *nonce, int nonce_size,
					 const uint8_t *tag, int tag_size)
{
	test_aes_gcm_encrypt(tmp, key, key_size, plaintext, ciphertext,
			     plaintext_size, nonce, nonce_size, tag, tag_size);

	test_aes_gcm_decrypt(tmp, key, key_size, plaintext, ciphertext,
			     plaintext_size, nonce, nonce_size, tag, tag_size);
}

static void test_aes_gcm_raw(const uint8_t *key, int key_size,
			     const uint8_t *plaintext,
			     const uint8_t *ciphertext,
			     std::size_t plaintext_size, const uint8_t *nonce,
			     std::size_t nonce_size, const uint8_t *tag,
			     std::size_t tag_size)
{
	zassert_true(plaintext_size <= sizeof(tmp));

	test_aes_gcm_raw_non_inplace(key, key_size, plaintext, ciphertext,
				     plaintext_size, nonce, nonce_size, tag,
				     tag_size);
	test_aes_gcm_raw_inplace(key, key_size, plaintext, ciphertext,
				 plaintext_size, nonce, nonce_size, tag,
				 tag_size);
}

ZTEST(aes, test_aes_gcm)
{
	/*
	 * Test vectors from BoringSSL
	 * https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt
	 * (only the ones with actual data, and no additional data).
	 */

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#4
	const TestVectorHex test_vector1 = {
		.key = "d480429666d48b400633921c5407d1d1",
		.plaintext = "",
		.nonce = "3388c676dc754acfa66e172a",
		.ciphertext = "",
		.tag = "7d7daf44850921a34e636b01adeb104f",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#424
	const TestVectorHex test_vector2 = {
		.key = "31323334353637383930313233343536",
		.plaintext = "48656c6c6f2c20576f726c64",
		.nonce = "31323334353637383930313233343536",
		.ciphertext = "cec189d0e8419b90fb16d555",
		.tag = "32893832a8d609224d77c2e56a922282",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#433
	const TestVectorHex test_vector3 = {
		.key = "00000000000000000000000000000000",
		.plaintext = "",
		.nonce = "000000000000000000000000",
		.ciphertext = "",
		.tag = "58e2fccefa7e3061367f1d57a4e7455a",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#440
	const TestVectorHex test_vector4 = {
		.key = "00000000000000000000000000000000",
		.plaintext = "",
		.nonce = "000000000000000000000000",
		.ciphertext = "",
		.tag = "58e2fccefa7e3061367f1d57a4e7455a",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#447
	const TestVectorHex test_vector5 = {
		.key = "feffe9928665731c6d6a8f9467308308",
		.plaintext =
			"d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
		.nonce = "cafebabefacedbaddecaf888",
		.ciphertext =
			"42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985",
		.tag = "4d5c2af327cd64a62cf35abd2ba6fab4",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#485
	const TestVectorHex test_vector6 = {
		.key = "00000000000000000000000000000000",
		.plaintext =
			"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.nonce = "000000000000000000000000",
		.ciphertext =
			"0388dace60b6a392f328c2b971b2fe78f795aaab494b5923f7fd89ff948bc1e0200211214e7394da2089b6acd093abe0",
		.tag = "9dd0a376b08e40eb00c35f29f9ea61a4",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#493
	const TestVectorHex test_vector7 = {
		.key = "00000000000000000000000000000000",
		.plaintext =
			"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.nonce = "000000000000000000000000",
		.ciphertext =
			"0388dace60b6a392f328c2b971b2fe78f795aaab494b5923f7fd89ff948bc1e0200211214e7394da2089b6acd093abe0c94da219118e297d7b7ebcbcc9c388f28ade7d85a8ee35616f7124a9d5270291",
		.tag = "98885a3a22bd4742fe7b72172193b163",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#501
	const TestVectorHex test_vector8 = {
		.key = "00000000000000000000000000000000",
		.plaintext =
			"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.nonce = "000000000000000000000000",
		.ciphertext =
			"0388dace60b6a392f328c2b971b2fe78f795aaab494b5923f7fd89ff948bc1e0200211214e7394da2089b6acd093abe0c94da219118e297d7b7ebcbcc9c388f28ade7d85a8ee35616f7124a9d527029195b84d1b96c690ff2f2de30bf2ec89e00253786e126504f0dab90c48a30321de3345e6b0461e7c9e6c6b7afedde83f40",
		.tag = "cac45f60e31efd3b5a43b98a22ce1aa1",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#509
	const TestVectorHex test_vector9 = {
		.key = "00000000000000000000000000000000",
		.plaintext =
			"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.nonce =
			"ffffffff000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.ciphertext =
			"56b3373ca9ef6e4a2b64fe1e9a17b61425f10d47a75a5fce13efc6bc784af24f4141bdd48cf7c770887afd573cca5418a9aeffcd7c5ceddfc6a78397b9a85b499da558257267caab2ad0b23ca476a53cb17fb41c4b8b475cb4f3f7165094c229c9e8c4dc0a2a5ff1903e501511221376a1cdb8364c5061a20cae74bc4acd76ceb0abc9fd3217ef9f8c90be402ddf6d8697f4f880dff15bfb7a6b28241ec8fe183c2d59e3f9dfff653c7126f0acb9e64211f42bae12af462b1070bef1ab5e3606",
		.tag = "566f8ef683078bfdeeffa869d751a017",
	};

	// https://boringssl.googlesource.com/boringssl/+/f94f3ed3965ea033001fb9ae006084eee408b861/crypto/cipher_extra/test/aes_128_gcm_tests.txt#517
	const TestVectorHex test_vector10 = {
		.key = "00000000000000000000000000000000",
		.plaintext =
			"000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.nonce =
			"ffffffff000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		.ciphertext =
			"56b3373ca9ef6e4a2b64fe1e9a17b61425f10d47a75a5fce13efc6bc784af24f4141bdd48cf7c770887afd573cca5418a9aeffcd7c5ceddfc6a78397b9a85b499da558257267caab2ad0b23ca476a53cb17fb41c4b8b475cb4f3f7165094c229c9e8c4dc0a2a5ff1903e501511221376a1cdb8364c5061a20cae74bc4acd76ceb0abc9fd3217ef9f8c90be402ddf6d8697f4f880dff15bfb7a6b28241ec8fe183c2d59e3f9dfff653c7126f0acb9e64211f42bae12af462b1070bef1ab5e3606872ca10dee15b3249b1a1b958f23134c4bccb7d03200bce420a2f8eb66dcf3644d1423c1b5699003c13ecef4bf38a3b60eedc34033bac1902783dc6d89e2e774188a439c7ebcc0672dbda4ddcfb2794613b0be41315ef778708a70ee7d75165c",
		.tag = "8b307f6b33286d0ab026a9ed3fe1e85f",
	};

	const std::array hex_test_vectors = { test_vector1, test_vector2,
					      test_vector3, test_vector4,
					      test_vector5, test_vector6,
					      test_vector7, test_vector8,
					      test_vector9, test_vector10 };

	std::vector<AesTestVector> test_vectors;
	for (const auto &hex_test_vector : hex_test_vectors) {
		AesTestVector test_vector;
		TestVectorHexToBytes(hex_test_vector, &test_vector);
		test_vectors.emplace_back(test_vector);
	}

	constexpr size_t kExpectedNumTestVectors = 10;
	zassert_equal(test_vectors.size(), kExpectedNumTestVectors);
	for (const auto &test_vector : test_vectors) {
		test_aes_gcm_raw(test_vector.key.data(), test_vector.key.size(),
				 test_vector.plaintext.data(),
				 test_vector.ciphertext.data(),
				 test_vector.plaintext.size(),
				 test_vector.nonce.data(),
				 test_vector.nonce.size(),
				 test_vector.tag.data(),
				 test_vector.tag.size());
	}
}

ZTEST(aes, test_aes_gcm_speed)
{
	Benchmark benchmark({ .num_iterations = 1000 });
	constexpr auto key = std::to_array<uint8_t>({
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	});
	std::array<uint8_t, 512> plaintext{};

	constexpr auto nonce = std::to_array<uint8_t>({
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	});
	std::array<uint8_t, 16> tag{};

	uint8_t *encrypted_data = tmp;

	zassert_true(plaintext.size() <= sizeof(tmp));

	benchmark.run("AES-GCM encrypt", [&]() {
		bssl::ScopedEVP_AEAD_CTX ctx;

		int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(),
					    key.data(), key.size(), tag.size(),
					    nullptr);
		zassert_true(ret == 1);

		size_t out_tag_len = 0;

		const std::span<uint8_t> extra_input; /* no extra input */
		const std::span<uint8_t> additional_data; /* no additional data
							   */

		ret = EVP_AEAD_CTX_seal_scatter(
			ctx.get(), encrypted_data, tag.data(), &out_tag_len,
			tag.size(), nonce.data(), nonce.size(),
			plaintext.data(), plaintext.size(), extra_input.data(),
			extra_input.size(), additional_data.data(),
			additional_data.size());
		zassert_true(ret == 1);
		zassert_true(out_tag_len == tag.size());
	});

	benchmark.run("AES-GCM decrypt", [&]() {
		bssl::ScopedEVP_AEAD_CTX ctx;

		int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(),
					    key.data(), key.size(), tag.size(),
					    nullptr);
		zassert_true(ret == 1);

		std::span<uint8_t> additional_data; /* no additional data */
		ret = EVP_AEAD_CTX_open_gather(ctx.get(), plaintext.data(),
					       nonce.data(), nonce.size(),
					       encrypted_data, plaintext.size(),
					       tag.data(), tag.size(),
					       additional_data.data(),
					       additional_data.size());
		zassert_true(ret == 1);
	});
	benchmark.print_results();
}

static void test_aes_raw(const uint8_t *key, int key_size,
			 const uint8_t *plaintext, const uint8_t *ciphertext)
{
	AES_KEY aes_key;
	uint8_t *block = tmp;

	zassert_true(AES_BLOCK_SIZE <= sizeof(tmp));

	zassert_true(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test encryption. */
	AES_encrypt(plaintext, block, &aes_key);
	zassert_mem_equal(ciphertext, block, AES_BLOCK_SIZE);

	/* Test in-place encryption. */
	memcpy(block, plaintext, AES_BLOCK_SIZE);
	AES_encrypt(block, block, &aes_key);
	zassert_mem_equal(ciphertext, block, AES_BLOCK_SIZE);

	zassert_true(AES_set_decrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test decryption. */
	AES_decrypt(ciphertext, block, &aes_key);
	zassert_mem_equal(plaintext, block, AES_BLOCK_SIZE);

	/* Test in-place decryption. */
	memcpy(block, ciphertext, AES_BLOCK_SIZE);
	AES_decrypt(block, block, &aes_key);
	zassert_mem_equal(plaintext, block, AES_BLOCK_SIZE);
}

ZTEST(aes, test_aes)
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

	test_aes_raw(key1, sizeof(key1), plain1, cipher1);
	test_aes_raw(key2, sizeof(key2), plain2, cipher2);
	test_aes_raw(key3, sizeof(key3), plain3, cipher3);
}

ZTEST(aes, test_aes_speed)
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
