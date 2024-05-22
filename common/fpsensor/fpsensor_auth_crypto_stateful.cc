/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO(b/286119221): refactor FPMCU code so that functions in this file don't
 * rely on global state. */

#include "compile_time_macros.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "openssl/bn.h"
#include "openssl/mem.h"
#include "openssl/rand.h"

#include <array>

enum ec_error_list
encrypt_data_in_place(uint16_t version,
		      struct fp_auth_command_encryption_metadata &info,
		      std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		      std::span<uint8_t> data)
{
	if (version != 1) {
		return EC_ERROR_INVAL;
	}

	info.struct_version = version;
	RAND_bytes(info.nonce, sizeof(info.nonce));
	RAND_bytes(info.encryption_salt, sizeof(info.encryption_salt));

	FpEncryptionKey enc_key;
	enum ec_error_list ret = derive_encryption_key(
		enc_key, info.encryption_salt, user_id, tpm_seed);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	/* Encrypt the secret blob in-place. */
	ret = aes_128_gcm_encrypt(enc_key, data, data, info.nonce, info.tag);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	return EC_SUCCESS;
}

std::optional<fp_encrypted_private_key> create_encrypted_private_key(
	const EC_KEY &key, uint16_t version,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	fp_encrypted_private_key enc_key;

	if (EC_KEY_priv2oct(&key, enc_key.data, sizeof(enc_key.data)) !=
	    sizeof(enc_key.data)) {
		return std::nullopt;
	}

	if (encrypt_data_in_place(version, enc_key.info, user_id, tpm_seed,
				  enc_key.data) != EC_SUCCESS) {
		return std::nullopt;
	}

	return enc_key;
}

enum ec_error_list
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	if (info.struct_version != 1) {
		return EC_ERROR_INVAL;
	}

	FpEncryptionKey enc_key;
	enum ec_error_list ret = derive_encryption_key(
		enc_key, info.encryption_salt, user_id, tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to derive key");
		return ret;
	}

	if (enc_data.size() != data.size()) {
		CPRINTS("Data size mismatch");
		return EC_ERROR_OVERFLOW;
	}

	ret = aes_128_gcm_decrypt(enc_key, data, enc_data, info.nonce,
				  info.tag);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to decipher data");
		return ret;
	}

	return EC_SUCCESS;
}

bssl::UniquePtr<EC_KEY> decrypt_private_key(
	const struct fp_encrypted_private_key &encrypted_private_key,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	CleanseWrapper<std::array<uint8_t, sizeof(encrypted_private_key.data)> >
		privkey;

	enum ec_error_list ret =
		decrypt_data(encrypted_private_key.info, user_id, tpm_seed,
			     encrypted_private_key.data, privkey);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to decrypt private key");
		return nullptr;
	}

	return create_ec_key_from_privkey(privkey.data(), privkey.size());
}
