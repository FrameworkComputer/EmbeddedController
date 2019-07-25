/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor crypto operations */

#ifndef __CROS_EC_FPSENSOR_CRYPTO_H
#define __CROS_EC_FPSENSOR_CRYPTO_H

/**
 * Derive hardware encryption key from rollback secret and |salt|.
 *
 * @param outkey the pointer to buffer holding the output key.
 * @param salt the salt to use in HKDF.
 * @return EC_SUCCESS on success and error code otherwise.
 */
int derive_encryption_key(uint8_t *out_key, const uint8_t *salt);

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
int aes_gcm_encrypt(const uint8_t *key, int key_size,
		    const uint8_t *plaintext,
		    uint8_t *ciphertext, int text_size,
		    const uint8_t *nonce, int nonce_size,
		    uint8_t *tag, int tag_size);

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
int aes_gcm_decrypt(const uint8_t *key, int key_size, uint8_t *plaintext,
		    const uint8_t *ciphertext, int text_size,
		    const uint8_t *nonce, int nonce_size,
		    const uint8_t *tag, int tag_size);

#endif /* __CROS_EC_FPSENSOR_CRYPTO_H */
