/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor crypto operations */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H

#include <cstdint>
#include <span>

extern "C" {
#include "common.h"
}

#define HKDF_MAX_INFO_SIZE 128
#define HKDF_SHA256_MAX_BLOCK_COUNT 255

/**
 * Expand hkdf pseudorandom key |prk| to length |out_key_size|.
 *
 * @param out_key the buffer to hold output key material.
 * @param out_key_size length of output key in bytes. Must be less than
 * or equal to HKDF_SHA256_MAX_BLOCK_COUNT * SHA256_DIGEST_SIZE bytes.
 * @param prk pseudorandom key.
 * @param prk_size length of |prk| in bytes.
 * @param info optional context.
 * @param info_size size of |info| in bytes, must be less than or equal to
 * HKDF_MAX_INFO_SIZE bytes.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list hkdf_expand(uint8_t *out_key, size_t out_key_size,
			       const uint8_t *prk, size_t prk_size,
			       const uint8_t *info, size_t info_size);

/**
 * Derive hardware encryption key from rollback secret, |salt|, and |info|.
 *
 * @param out_key the pointer to buffer holding the output key.
 * @param salt the salt to use in HKDF.
 * @param info the info to use in HKDF.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list
derive_encryption_key_with_info(std::span<uint8_t> out_key,
				std::span<const uint8_t> salt,
				std::span<const uint8_t> info);

/**
 * Call derive_encryption_key_with_info with the context user_id as |info|.
 */
enum ec_error_list derive_encryption_key(std::span<uint8_t> out_key,
					 std::span<const uint8_t> salt);

/**
 * Derive positive match secret from |input_positive_match_salt| and
 * SBP_Src_Key.
 *
 * @param output buffer to store positive match secret, must be at least
 * FP_POSITIVE_MATCH_SECRET_BYTES in size.
 * @param input_positive_match_salt the salt for deriving secret, must be at
 * least FP_POSITIVE_MATCH_SALT_BYTES in size.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list derive_positive_match_secret(
	std::span<uint8_t> output,
	std::span<const uint8_t> input_positive_match_salt);

/**
 * Encrypt |plaintext| using AES-GCM128.
 *
 * @param key the key to use in AES.
 * @param plaintext the plain text to encrypt.
 * @param ciphertext buffer to hold encryption result.
 * @param nonce the nonce value to use in GCM128.
 * @param tag the tag to hold the authenticator after encryption.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list aes_128_gcm_encrypt(std::span<const uint8_t> key,
				       std::span<const uint8_t> plaintext,
				       std::span<uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<uint8_t> tag);

/**
 * Decrypt |plaintext| using AES-GCM128.
 *
 * @param key the key to use in AES.
 * @param ciphertext the cipher text to decrypt.
 * @param plaintext buffer to hold decryption result.
 * @param nonce the nonce value to use in GCM128.
 * @param tag the tag to compare against when decryption finishes.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list aes_128_gcm_decrypt(std::span<const uint8_t> key,
				       std::span<uint8_t> plaintext,
				       std::span<const uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<const uint8_t> tag);

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H */
