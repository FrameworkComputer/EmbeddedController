/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"

#include <array>

/* Boringssl headers need to be included before extern "C" section. */
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ecdh.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"

extern "C" {
#include "ec_commands.h"
#include "sha256.h"
}

#include "fpsensor/fpsensor_auth_crypto.h"

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
					       uint8_t *shared_secret,
					       uint8_t shared_secret_size)
{
	const EC_POINT *public_point = EC_KEY_get0_public_key(&public_key);
	if (public_point == nullptr) {
		return EC_ERROR_INVAL;
	}

	if (ECDH_compute_key_fips(shared_secret, shared_secret_size,
				  public_point, &private_key) != 1) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

enum ec_error_list
generate_gsc_session_key(std::span<const uint8_t> auth_nonce,
			 std::span<const uint8_t> gsc_nonce,
			 std::span<const uint8_t> pairing_key,
			 std::span<uint8_t> gsc_session_key)
{
	if (auth_nonce.size() != 32 || gsc_nonce.size() != 32 ||
	    pairing_key.size() != 32 ||
	    gsc_session_key.size() != SHA256_DIGEST_SIZE) {
		return EC_ERROR_INVAL;
	}
	CleanseWrapper<struct sha256_ctx> ctx;
	SHA256_init(&ctx);
	SHA256_update(&ctx, auth_nonce.data(), auth_nonce.size());
	SHA256_update(&ctx, gsc_nonce.data(), gsc_nonce.size());
	SHA256_update(&ctx, pairing_key.data(), pairing_key.size());
	uint8_t *result = SHA256_final(&ctx);

	std::copy(result, result + SHA256_DIGEST_SIZE, gsc_session_key.data());

	return EC_SUCCESS;
}

enum ec_error_list decrypt_data_with_gsc_session_key_in_place(
	std::span<const uint8_t> gsc_session_key, std::span<const uint8_t> iv,
	std::span<uint8_t> data)
{
	if (gsc_session_key.size() != 32 || iv.size() != AES_BLOCK_SIZE) {
		return EC_ERROR_INVAL;
	}

	CleanseWrapper<AES_KEY> aes_key;
	int res = AES_set_encrypt_key(gsc_session_key.data(), 256, &aes_key);
	if (res) {
		return EC_ERROR_INVAL;
	}

	std::array<uint8_t, AES_BLOCK_SIZE> aes_iv;
	std::copy(iv.begin(), iv.begin() + iv.size(), aes_iv.begin());

	/* The AES CTR uses the same function for encryption & decryption. */
	unsigned int block_num = 0;
	std::array<uint8_t, AES_BLOCK_SIZE> ecount_buf;
	AES_ctr128_encrypt(data.data(), data.data(), data.size(), &aes_key,
			   aes_iv.data(), ecount_buf.data(), &block_num);

	return EC_SUCCESS;
}

enum ec_error_list encrypt_data_with_ecdh_key_in_place(
	const struct fp_elliptic_curve_public_key &in_pubkey,
	std::span<uint8_t> data, std::span<uint8_t> iv,
	struct fp_elliptic_curve_public_key &out_pubkey)
{
	if (iv.size() != AES_BLOCK_SIZE) {
		return EC_ERROR_INVAL;
	}

	bssl::UniquePtr<EC_KEY> private_key = generate_elliptic_curve_key();
	if (private_key == nullptr) {
		return EC_ERROR_MEMORY_ALLOCATION;
	}

	std::optional<fp_elliptic_curve_public_key> out_key =
		create_pubkey_from_ec_key(*private_key);
	if (!out_key.has_value()) {
		return EC_ERROR_INVAL;
	}

	out_pubkey = out_key.value();

	bssl::UniquePtr<EC_KEY> public_key =
		create_ec_key_from_pubkey(in_pubkey);
	if (public_key == nullptr) {
		return EC_ERROR_MEMORY_ALLOCATION;
	}

	CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_SIZE> > enc_key;

	enum ec_error_list ret = generate_ecdh_shared_secret(
		*private_key, *public_key, enc_key.data(), enc_key.size());
	if (ret != EC_SUCCESS) {
		return ret;
	}

	CleanseWrapper<AES_KEY> aes_key;
	int res = AES_set_encrypt_key(enc_key.data(), 256, &aes_key);
	if (res) {
		return EC_ERROR_INVAL;
	}

	RAND_bytes(iv.data(), iv.size());

	/* The IV will be changed after the AES_ctr128_encrypt, we need a copy
	 * for that. */
	std::array<uint8_t, AES_BLOCK_SIZE> aes_iv;

	std::copy(iv.begin(), iv.begin() + iv.size(), aes_iv.begin());

	unsigned int block_num = 0;
	std::array<uint8_t, AES_BLOCK_SIZE> ecount_buf;

	/* The AES CTR uses the same function for encryption & decryption. */
	AES_ctr128_encrypt(data.data(), data.data(), data.size(), &aes_key,
			   aes_iv.data(), ecount_buf.data(), &block_num);

	return EC_SUCCESS;
}
