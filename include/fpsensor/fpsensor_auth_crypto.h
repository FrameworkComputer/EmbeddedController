/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H

#include "openssl/ec.h"

#include <span>

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
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 * @param[in,out] data the data that need to be encrypted in place
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list
encrypt_data_in_place(uint16_t version,
		      struct fp_auth_command_encryption_metadata &info,
		      std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		      std::span<uint8_t> data);

/**
 * Encrypt the @p EC_KEY with a specific version of encryption method.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] key the private
 * @param[in] version the version of the encryption method
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 *
 * @return @p fp_encrypted_private_key on success
 * @return std::nullopt on error
 */
std::optional<fp_encrypted_private_key> create_encrypted_private_key(
	const EC_KEY &key, uint16_t version,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

/**
 * Decrypt the encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] info the metadata of the encryption output
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 * @param[in] enc_data the encrypted data
 * @param[out] data the decrypted data
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> enc_data, std::span<uint8_t> data);

/**
 * Decrypt the encrypted private key.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] encrypted_private_key encrypted private key
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
bssl::UniquePtr<EC_KEY> decrypt_private_key(
	const struct fp_encrypted_private_key &encrypted_private_key,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

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
 * @param[in] gsc_nonce the auth nonce
 * @param[in] pairing_key the auth nonce
 * @param[in,out] gsc_session_key the output key
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list
generate_gsc_session_key(std::span<const uint8_t> auth_nonce,
			 std::span<const uint8_t> gsc_nonce,
			 std::span<const uint8_t> pairing_key,
			 std::span<uint8_t> gsc_session_key);

/**
 * Decrypt the data in place with a GSC session key.
 * Note: The GSC session key is equal to the CK in the original design doc.
 *
 * @param[in] gsc_session_key the GSC session key
 * @param[in] iv the IV of the encrypted data
 * @param[in,out] data the encrypted data
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list decrypt_data_with_gsc_session_key_in_place(
	std::span<const uint8_t> gsc_session_key, std::span<const uint8_t> iv,
	std::span<uint8_t> data);
/**
 * Encrypt the data with a ECDH public key.
 *
 * @param[in] in_pubkey the input public key
 * @param[in,out] data the data to be encrypted
 * @param[out] iv the output IV
 * @param[out] out_pubkey the output public key
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list encrypt_data_with_ecdh_key_in_place(
	const struct fp_elliptic_curve_public_key &in_pubkey,
	std::span<uint8_t> data, std::span<uint8_t> iv,
	struct fp_elliptic_curve_public_key &out_pubkey);

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H */
