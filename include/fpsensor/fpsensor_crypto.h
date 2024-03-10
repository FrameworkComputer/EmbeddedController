/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor crypto operations */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H

#include "compile_time_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "sha256.h"

#include <stddef.h>
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
 * @param outkey the pointer to buffer holding the output key.
 * @param salt the salt to use in HKDF.
 * @param info the info to use in HKDF.
 * @param info_size the size of |info| in bytes.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list derive_encryption_key_with_info(uint8_t *out_key,
						   const uint8_t *salt,
						   const uint8_t *info,
						   size_t info_size);

/**
 * Call derive_encryption_key_with_info with the context user_id as |info|.
 */
enum ec_error_list derive_encryption_key(uint8_t *out_key, const uint8_t *salt);

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
enum ec_error_list
derive_positive_match_secret(uint8_t *output,
			     const uint8_t *input_positive_match_salt);

/**
 * Encrypt |plaintext| using AES-GCM128.
 *
 * @param key the key to use in AES.
 * @param key_size the size of |key| in bytes.
 * @param plaintext the plain text to encrypt.
 * @param ciphertext buffer to hold encryption result.
 * @param text_size size of both |plaintext| and output ciphertext in bytes.
 * @param nonce the nonce value to use in GCM128.
 * @param nonce_size the size of |nonce| in bytes.
 * @param tag the tag to hold the authenticator after encryption.
 * @param tag_size the size of |tag|.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list aes_128_gcm_encrypt(const uint8_t *key, size_t key_size,
				       const uint8_t *plaintext,
				       uint8_t *ciphertext, size_t text_size,
				       const uint8_t *nonce, size_t nonce_size,
				       uint8_t *tag, size_t tag_size);

/**
 * Decrypt |plaintext| using AES-GCM128.
 *
 * @param key the key to use in AES.
 * @param key_size the size of |key| in bytes.
 * @param ciphertext the cipher text to decrypt.
 * @param plaintext buffer to hold decryption result.
 * @param text_size size of both |ciphertext| and output plaintext in bytes.
 * @param nonce the nonce value to use in GCM128.
 * @param nonce_size the size of |nonce| in bytes.
 * @param tag the tag to compare against when decryption finishes.
 * @param tag_size the length of tag to compare against.
 * @return EC_SUCCESS on success and error code otherwise.
 */
enum ec_error_list aes_128_gcm_decrypt(const uint8_t *key, size_t key_size,
				       uint8_t *plaintext,
				       const uint8_t *ciphertext,
				       size_t text_size, const uint8_t *nonce,
				       size_t nonce_size, const uint8_t *tag,
				       size_t tag_size);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_CRYPTO_H */
