/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_crypto.h"
#include "openssl/aead.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ecdh.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"

#include <algorithm>
#include <array>

std::optional<fp_elliptic_curve_public_key>
create_pubkey_from_ec_key(const EC_KEY &key)
{
	fp_elliptic_curve_public_key pubkey;
	static_assert(sizeof(pubkey) == sizeof(pubkey.x) + sizeof(pubkey.y));

	/* POINT_CONVERSION_UNCOMPRESSED indicates that the point is encoded as
	 * z||x||y, where z is the octet 0x04. */
	uint8_t *data = nullptr;
	if (EC_KEY_key2buf(&key, POINT_CONVERSION_UNCOMPRESSED, &data,
			   nullptr) !=
	    sizeof(pubkey.x) + sizeof(pubkey.y) + 1) {
		return std::nullopt;
	}

	bssl::UniquePtr<uint8_t> pubkey_data(data);
	uint8_t *pubkey_ptr = reinterpret_cast<uint8_t *>(&pubkey);
	std::copy(pubkey_data.get() + 1,
		  pubkey_data.get() + 1 + sizeof(pubkey.x) + sizeof(pubkey.y),
		  pubkey_ptr);

	return pubkey;
}

bssl::UniquePtr<EC_KEY>
create_ec_key_from_pubkey(const fp_elliptic_curve_public_key &pubkey)
{
	bssl::UniquePtr<BIGNUM> x_bn(
		BN_bin2bn(pubkey.x, sizeof(pubkey.x), nullptr));
	if (x_bn == nullptr) {
		return nullptr;
	}

	bssl::UniquePtr<BIGNUM> y_bn(
		BN_bin2bn(pubkey.y, sizeof(pubkey.y), nullptr));
	if (y_bn == nullptr) {
		return nullptr;
	}

	static_assert(sizeof(pubkey.x) == 32);
	static_assert(sizeof(pubkey.y) == 32);
	bssl::UniquePtr<EC_KEY> key(
		EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
	if (key == nullptr) {
		return nullptr;
	}

	if (EC_KEY_set_public_key_affine_coordinates(key.get(), x_bn.get(),
						     y_bn.get()) != 1) {
		return nullptr;
	}

	return key;
}

bssl::UniquePtr<EC_KEY> create_ec_key_from_privkey(const uint8_t *privkey,
						   size_t privkey_size)
{
	bssl::UniquePtr<EC_KEY> key(
		EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
	if (key == nullptr) {
		return nullptr;
	}

	if (EC_KEY_oct2priv(key.get(), privkey, privkey_size) != 1) {
		return nullptr;
	}

	return key;
}

enum ec_error_list generate_ecdh_shared_secret(const EC_KEY &private_key,
					       const EC_KEY &public_key,
					       std::span<uint8_t> secret)
{
	const EC_POINT *public_point = EC_KEY_get0_public_key(&public_key);
	if (public_point == nullptr) {
		return EC_ERROR_INVAL;
	}

	if (ECDH_compute_key_fips(secret.data(), secret.size(), public_point,
				  &private_key) != 1) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

enum ec_error_list
generate_ecdh_shared_secret_without_kdf(const EC_KEY &private_key,
					const EC_KEY &public_key,
					std::span<uint8_t> secret)
{
	const EC_POINT *public_point = EC_KEY_get0_public_key(&public_key);
	if (public_point == nullptr) {
		return EC_ERROR_INVAL;
	}

	if (ECDH_compute_key(secret.data(), secret.size(), public_point,
			     &private_key, NULL) == -1) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

enum ec_error_list generate_session_key(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t> session_context,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key)
{
	std::array inputs{
		std::span<const uint8_t>{ fpmcu_nonce },
		std::span<const uint8_t>{ peer_nonce },
		session_context,
	};

	return hmac_sha256(pairing_key, inputs, session_key);
}

enum ec_error_list decrypt_data_with_session_key(
	std::span<const uint8_t, 32> session_key,
	std::span<const uint8_t> input, std::span<uint8_t> output,
	std::span<const uint8_t, FP_AES_KEY_NONCE_BYTES> nonce,
	std::span<const uint8_t, FP_AES_KEY_TAG_BYTES> tag,
	std::span<const uint8_t> aad)
{
	if (input.size() != output.size()) {
		return EC_ERROR_OVERFLOW;
	}

	bssl::ScopedEVP_AEAD_CTX ctx;
	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(),
				    session_key.data(), session_key.size(),
				    tag.size(), nullptr);
	if (!ret) {
		return EC_ERROR_INVAL;
	}

	ret = EVP_AEAD_CTX_open_gather(ctx.get(), output.data(), nonce.data(),
				       nonce.size(), input.data(), input.size(),
				       tag.data(), tag.size(), aad.data(),
				       aad.size());
	if (!ret) {
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

enum ec_error_list compute_message_signature(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> context, std::span<const uint8_t> sender,
	std::span<const uint8_t> operation,
	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> signature)
{
	std::array inputs{
		std::span<const uint8_t>{ challenge },
		context,
		sender,
		operation,
	};

	return hmac_sha256(session_key, inputs, signature);
}
