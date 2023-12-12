/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H

#include "openssl/ec.h"

extern "C" {
#include "ec_commands.h"
}

#include <optional>

/**
 * Create a @fp_elliptic_curve_public_key with the content of boringssl @p
 * EC_KEY.
 *
 * @param[in] key boringssl key
 *
 * @return @p fp_elliptic_curve_public_key on success
 * @return std::nullopt on error
 */
std::optional<fp_elliptic_curve_public_key>
create_pubkey_from_ec_key(const EC_KEY &key);

/**
 * Create a boringssl @EC_KEY from the @p fp_elliptic_curve_public_key content.
 *
 * @param[in] pubkey public key structure
 *
 * @return @p EC_KEY on success
 * @return nullptr on error
 */
bssl::UniquePtr<EC_KEY>
create_ec_key_from_pubkey(const fp_elliptic_curve_public_key &pubkey);

/**
 * Create a boringssl @EC_KEY from a private key.
 *
 * @param[in] privkey private key
 *
 * @return @p EC_KEY on success
 * @return nullptr on error
 */
bssl::UniquePtr<EC_KEY> create_ec_key_from_privkey(const uint8_t *privkey,
						   size_t privkey_size);

/**
 * Encrypt the data in place with a specific version of encryption method and
 * output the metadata and encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] version the version of the encryption method
 * @param[out] info the metadata of the encryption output
 * @param[in,out] data the data that need to be encrypted in place
 * @param[in] data_size the size of data
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list
encrypt_data_in_place(uint16_t version,
		      struct fp_auth_command_encryption_metadata &info,
		      uint8_t *data, size_t data_size);

/**
 * Encrypt the @p EC_KEY with a specific version of encryption method.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] key the private
 * @param[in] version the version of the encryption method
 * @param[out] enc_key the encryption output
 *
 * @return @p fp_encrypted_private_key on success
 * @return std::nullopt on error
 */
std::optional<fp_encrypted_private_key>
create_encrypted_private_key(const EC_KEY &key, uint16_t version);

/**
 * Decrypt the encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] info the metadata of the encryption output
 * @param[in] enc_data the encrypted data
 * @param[in] enc_data_size the size of encrypted data
 * @param[in] version the version of the encryption method
 * @param[out] data the decrypted data
 * @param[in] data_size the size of decrypted data
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     const uint8_t *enc_data, size_t enc_data_size, uint8_t *data,
	     size_t data_size);

/**
 * Decrypt the encrypted private key.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] info the metadata of the encryption output
 * @param[in] enc_data the encrypted data
 * @param[in] enc_data_size the size of encrypted data
 * @param[in] version the version of the encryption method
 * @param[out] data the decrypted data
 * @param[in] data_size the size of decrypted data
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
bssl::UniquePtr<EC_KEY> decrypt_private_key(
	const struct fp_encrypted_private_key &encrypted_private_key);

/**
 * Generate the ECDH shared secret from private key and public key.
 *
 * @param[in] private_key the private key of the ECDH
 * @param[in] public_key the public key of the ECDH
 * @param[out] shared_secret the shared secret
 * @param[in] share_secret_size the size of shared secret
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list generate_ecdh_shared_secret(const EC_KEY &private_key,
					       const EC_KEY &public_key,
					       uint8_t *shared_secret,
					       uint8_t share_secret_size);

/**
 * Generate a gsc_session_key that is derived from auth nonce, GSC nonce and
 * pairing key.
 *
 * @param[in] auth_nonce the auth nonce
 * @param[in] auth_nonce_size the size of auth nonce
 * @param[in] gsc_nonce the auth nonce
 * @param[in] gsc_nonce_size the size of gsc nonce
 * @param[in] pairing_key the auth nonce
 * @param[in] pairing_key_size the size of pairing key
 * @param[in,out] gsc_session_key the output key
 * @param[in] gsc_session_key_size the output key size
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list
generate_gsc_session_key(const uint8_t *auth_nonce, size_t auth_nonce_size,
			 const uint8_t *gsc_nonce, size_t gsc_nonce_size,
			 const uint8_t *pairing_key, size_t pairing_key_size,
			 uint8_t *gsc_session_key, size_t gsc_session_key_size);

/**
 * Decrypt the data in place with a GSC session key.
 * Note: The GSC session key is equal to the CK in the original design doc.
 *
 * @param[in] gsc_session_key the GSC session key
 * @param[in] gsc_session_key_size the size of GSC session key
 * @param[in] iv the IV of the encrypted data
 * @param[in] iv_size the size of the IV
 * @param[in,out] data the encrypted data
 * @param[in] data_size the output data size
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list decrypt_data_with_gsc_session_key_in_place(
	const uint8_t *gsc_session_key, size_t gsc_session_key_size,
	const uint8_t *iv, size_t iv_size, uint8_t *data, size_t data_size);
/**
 * Encrypt the data with a ECDH public key.
 *
 * @param[in] in_pubkey the input public key
 * @param[in,out] data the data to be encrypted
 * @param[in] data_size the data size
 * @param[out] iv the output IV
 * @param[in] iv_size the IV size
 * @param[out] out_pubkey the output public key
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list encrypt_data_with_ecdh_key_in_place(
	const struct fp_elliptic_curve_public_key &in_pubkey, uint8_t *data,
	size_t data_size, uint8_t *iv, size_t iv_size,
	struct fp_elliptic_curve_public_key &out_pubkey);

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H */
