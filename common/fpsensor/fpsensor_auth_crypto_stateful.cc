/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO(b/286119221): refactor FPMCU code so that functions in this file don't
 * rely on global state. */

#include "compile_time_macros.h"

/* Boringssl headers need to be included before extern "C" section. */
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"

extern "C" {
#include "ec_commands.h"
#include "sha256.h"
}

// clang-format off
#include <array>

#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state_without_driver_info.h"
#include "fpsensor/fpsensor_utils.h"
// clang-format on

enum ec_error_list
encrypt_data_in_place(uint16_t version,
		      struct fp_auth_command_encryption_metadata &info,
		      uint8_t *data, size_t data_size)
{
	if (version != 1) {
		return EC_ERROR_INVAL;
	}

	info.struct_version = version;
	RAND_bytes(info.nonce, sizeof(info.nonce));
	RAND_bytes(info.encryption_salt, sizeof(info.encryption_salt));

	CleanseWrapper<std::array<uint8_t, SBP_ENC_KEY_LEN> > enc_key;
	enum ec_error_list ret =
		derive_encryption_key(enc_key.data(), info.encryption_salt);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	/* Encrypt the secret blob in-place. */
	ret = aes_128_gcm_encrypt(enc_key.data(), enc_key.size(), data, data,
				  data_size, info.nonce, sizeof(info.nonce),
				  info.tag, sizeof(info.tag));
	if (ret != EC_SUCCESS) {
		return ret;
	}

	return EC_SUCCESS;
}

std::optional<fp_encrypted_private_key>
create_encrypted_private_key(const EC_KEY &key, uint16_t version)
{
	fp_encrypted_private_key enc_key;

	if (EC_KEY_priv2oct(&key, enc_key.data, sizeof(enc_key.data)) !=
	    sizeof(enc_key.data)) {
		return std::nullopt;
	}

	if (encrypt_data_in_place(version, enc_key.info, enc_key.data,
				  sizeof(enc_key.data)) != EC_SUCCESS) {
		return std::nullopt;
	}

	return enc_key;
}

enum ec_error_list
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     const uint8_t *enc_data, size_t enc_data_size, uint8_t *data,
	     size_t data_size)
{
	if (info.struct_version != 1) {
		return EC_ERROR_INVAL;
	}

	CleanseWrapper<std::array<uint8_t, SBP_ENC_KEY_LEN> > enc_key;
	enum ec_error_list ret =
		derive_encryption_key(enc_key.data(), info.encryption_salt);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to derive key");
		return ret;
	}

	if (enc_data_size != data_size) {
		CPRINTS("Data size mismatch");
		return EC_ERROR_OVERFLOW;
	}

	ret = aes_128_gcm_decrypt(enc_key.data(), enc_key.size(), data,
				  enc_data, data_size, info.nonce,
				  sizeof(info.nonce), info.tag,
				  sizeof(info.tag));
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to decipher data");
		return ret;
	}

	return EC_SUCCESS;
}

bssl::UniquePtr<EC_KEY> decrypt_private_key(
	const struct fp_encrypted_private_key &encrypted_private_key)
{
	CleanseWrapper<std::array<uint8_t, sizeof(encrypted_private_key.data)> >
		privkey;

	enum ec_error_list ret = decrypt_data(
		encrypted_private_key.info, encrypted_private_key.data,
		sizeof(encrypted_private_key.data), privkey.data(),
		privkey.size());
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to decrypt private key");
		return nullptr;
	}

	return create_ec_key_from_privkey(privkey.data(), privkey.size());
}
