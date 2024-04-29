/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file fpsensor_crypto_mock.cc
 * @brief Mock fpsensor_crypto library
 */
#include "assert.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_utils.h"
#include "mock/fpsensor_crypto_mock.h"

#include <algorithm>
#include <span>

extern "C" {
#include "sha256.h"
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_ctrl_fpsensor_crypto mock_ctrl_fpsensor_crypto =
	MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO;

#define MESSAGE_ZERO_IDX 0
#define MESSAGE_FF_IDX 1
typedef uint8_t key_message_pair[FP_POSITIVE_MATCH_SECRET_BYTES];

key_message_pair fake_fpsensor_crypto[] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
};

BUILD_ASSERT(sizeof(key_message_pair) == FP_POSITIVE_MATCH_SECRET_BYTES);

/* Mock compute_hmac_sha256 for unit or fuzz tests. */
extern "C" void compute_hmac_sha256(uint8_t *output, const uint8_t *key,
				    const int key_len, const uint8_t *message,
				    const int message_len)
{
	switch (mock_ctrl_fpsensor_crypto.output_type) {
	case MOCK_CTRL_FPSENSOR_CRYPTO_SHA256_TYPE_REAL:
		hmac_SHA256(output, key, key_len, message, message_len);
		break;
	case MOCK_CTRL_FPSENSOR_CRYPTO_SHA256_TYPE_ZEROS:
		memcpy(output, fake_fpsensor_crypto[MESSAGE_ZERO_IDX],
		       FP_POSITIVE_MATCH_SECRET_BYTES);
		break;
	case MOCK_CTRL_FPSENSOR_CRYPTO_SHA256_TYPE_FF:
		memcpy(output, fake_fpsensor_crypto[MESSAGE_FF_IDX],
		       FP_POSITIVE_MATCH_SECRET_BYTES);
		break;
	default:
		assert(0);
		break;
	};
}

extern bool hkdf_sha256_impl(std::span<uint8_t> out_key,
			     std::span<const uint8_t> ikm,
			     std::span<const uint8_t> salt,
			     std::span<const uint8_t> info);

bool hkdf_sha256(std::span<uint8_t> out_key, std::span<const uint8_t> ikm,
		 std::span<const uint8_t> salt, std::span<const uint8_t> info)
{
	switch (mock_ctrl_fpsensor_crypto.output_type) {
	case MOCK_CTRL_FPSENSOR_CRYPTO_SHA256_TYPE_REAL:
		return hkdf_sha256_impl(out_key, ikm, salt, info);
	case MOCK_CTRL_FPSENSOR_CRYPTO_SHA256_TYPE_ZEROS:
		std::ranges::fill(out_key, 0);
		return true;
	case MOCK_CTRL_FPSENSOR_CRYPTO_SHA256_TYPE_FF:
		std::ranges::fill(out_key, 0xFF);
		return true;
	default:
		assert(0);
		return false;
	};
}
