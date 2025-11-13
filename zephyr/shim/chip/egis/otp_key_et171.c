/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* One-Time Programmable (OTP) Key */

#include "openssl/mem.h"
#include "otp_key.h"
#include "util.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <et171_hal/et171_hal_otp.h>

LOG_MODULE_REGISTER(egis_otp_key, LOG_LEVEL_ERR);

/*
 * The base Word offset for the EGIS ET171 EGIS_KEY area in the OTP
 * memory map. This entire area is locked.
 */
static const uint32_t key_base_word_offset = 0x64;

/* Index of the secret key (EGIS_KEY3) within the EGIS_KEY OTP region */
static const uint32_t key_index = 2;

/*
 * The size of the EGIS ET171 OTP key sub-unit, expressed in words (0x8
 * words). For a 4-byte word, this corresponds to 32 bytes (8 words * 4
 * bytes/word).
 */
static const uint32_t key_size_words = 0x8;

BUILD_ASSERT(OTP_KEY_SIZE_BYTES / sizeof(uint32_t) == key_size_words);
BUILD_ASSERT(OTP_KEY_SIZE_BYTES % sizeof(uint32_t) == 0);

void otp_key_init(void)
{
	/* Nothing to do */
}

void otp_key_exit(void)
{
	/* Nothing to do */
}

enum ec_error_list otp_key_read(uint8_t *key_buffer)
{
	if (key_buffer == NULL) {
		return EC_ERROR_INVAL;
	}

	/* Calculated starting Word offset of EGIS_KEY3 in the OTP memory. */
	uint32_t otp_key_word_addr =
		key_base_word_offset + key_index * key_size_words;

	if (HAL_OTP_Read32(otp_key_word_addr, key_size_words,
			   (uint32_t *)key_buffer) != HAL_OK) {
		LOG_ERR("Failed to read OTP key from HAL");
		return EC_ERROR_HW_INTERNAL;
	}

	return EC_SUCCESS;
}

enum ec_error_list otp_key_provision(void)
{
	uint8_t otp_key_buffer[OTP_KEY_SIZE_BYTES] __aligned(4) = { 0 };

	enum ec_error_list ec_status = otp_key_read(otp_key_buffer);
	if (ec_status != EC_SUCCESS) {
		LOG_ERR("Failed to read OTP key with status=%d", ec_status);
		return ec_status;
	}

	/*
	 * If the stored bytes are trivial (all 0's or all 1's), this indicates
	 * a potential provisioning failure during factory setup.
	 */
	if (bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES)) {
		uint8_t first_byte = otp_key_buffer[0];
		LOG_ERR("%s OTP key is trivial (all 0x%02x)!\n", __func__,
			first_byte);
		return EC_ERROR_HW_INTERNAL;
	}

	OPENSSL_cleanse(otp_key_buffer, OTP_KEY_SIZE_BYTES);

	return EC_SUCCESS;
}
