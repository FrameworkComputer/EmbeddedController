/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor crypto operations */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"

#include <cstdint>
#include <span>

/**
 * A buffer holding an encryption key. Automatically cleared on destruction.
 */
using FpEncryptionKey = CleanseWrapper<std::array<uint8_t, 16> >;
BUILD_ASSERT(sizeof(FpEncryptionKey) == 16, "Encryption key must be 128 bits.");
BUILD_ASSERT(sizeof(FpEncryptionKey) <= CONFIG_ROLLBACK_SECRET_SIZE);

/**
 * Computes HKDF (as specified by RFC 5869) using SHA-256 as the digest.
 *
 * @param[out] out_key buffer to hold output key material. Max size must be less
 * than or equal to 255 * 32 (SHA256_DIGEST_SIZE) bytes = 8160 bytes.
 * @param[in] ikm input keying material.
 * @param[in] salt optional salt value (a non-secret random value).
 * @param[in] info optional context and application specific information (can be
 * a zero-length string).
 * @return true on success
 * @return false on failure
 */
bool hkdf_sha256(std::span<uint8_t> out_key, std::span<const uint8_t> ikm,
		 std::span<const uint8_t> salt, std::span<const uint8_t> info);

/**
 * Derive hardware encryption key from rollback secret, |salt|, and |info|.
 *
 * @param out_key the pointer to buffer holding the output key.
 * @param salt the salt to use in HKDF.
 * @param info the info to use in HKDF.
 * @param tpm_seed the seed from the TPM for deriving secret.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list
derive_encryption_key(std::span<uint8_t> out_key, std::span<const uint8_t> salt,
		      std::span<const uint8_t> info,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

/**
 * Derive positive match secret from |input_positive_match_salt| and
 * SBP_Src_Key.
 *
 * @param output buffer to store positive match secret, must be at least
 * FP_POSITIVE_MATCH_SECRET_BYTES in size.
 * @param input_positive_match_salt the salt for deriving secret, must be at
 * least FP_POSITIVE_MATCH_SALT_BYTES in size.
 * @param user_id the user_id used for deriving secret.
 * @param tpm_seed the seed from the TPM for deriving secret.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list derive_positive_match_secret(
	std::span<uint8_t> output,
	std::span<const uint8_t> input_positive_match_salt,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

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
