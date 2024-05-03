/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state_without_driver_info.h"
#include "openssl/aead.h"
#include "openssl/evp.h"
#include "openssl/hkdf.h"
#include "openssl/mem.h"

#include <span>

extern "C" {
#include "otp_key.h"
#include "rollback.h"
#include "sha256.h"
#include "util.h"

test_mockable void compute_hmac_sha256(uint8_t *output, const uint8_t *key,
				       const int key_len,
				       const uint8_t *message,
				       const int message_len);
}

#include <stdbool.h>

#ifdef CONFIG_OTP_KEY
constexpr uint8_t IKM_OTP_OFFSET_BYTES =
	CONFIG_ROLLBACK_SECRET_SIZE + sizeof(tpm_seed);
constexpr uint8_t IKM_SIZE_BYTES = IKM_OTP_OFFSET_BYTES + OTP_KEY_SIZE_BYTES;
BUILD_ASSERT(IKM_SIZE_BYTES == 96);

#else
constexpr uint8_t IKM_SIZE_BYTES =
	CONFIG_ROLLBACK_SECRET_SIZE + sizeof(tpm_seed);
BUILD_ASSERT(IKM_SIZE_BYTES == 64);
#endif

#if !defined(CONFIG_BORINGSSL_CRYPTO) || !defined(CONFIG_ROLLBACK_SECRET_SIZE)
#error "fpsensor requires CONFIG_BORINGSSL_CRYPTO and ROLLBACK_SECRET_SIZE"
#endif

test_export_static enum ec_error_list
get_ikm(std::span<uint8_t, IKM_SIZE_BYTES> ikm)
{
	enum ec_error_list ret;

	if (!fp_tpm_seed_is_set()) {
		CPRINTS("Seed hasn't been set.");
		return EC_ERROR_ACCESS_DENIED;
	}

	/*
	 * The first CONFIG_ROLLBACK_SECRET_SIZE bytes of IKM are read from the
	 * anti-rollback blocks.
	 */
	ret = rollback_get_secret(ikm.data());
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read rollback secret: %d", ret);
		return EC_ERROR_HW_INTERNAL;
	}
	/*
	 * IKM is the concatenation of the rollback secret and the seed from
	 * the TPM.
	 */
	memcpy(ikm.data() + CONFIG_ROLLBACK_SECRET_SIZE, tpm_seed,
	       sizeof(tpm_seed));

#ifdef CONFIG_OTP_KEY
	uint8_t otp_key[OTP_KEY_SIZE_BYTES] = { 0 };

	otp_key_init();
	ret = (enum ec_error_list)otp_key_read(otp_key);
	otp_key_exit();

	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read OTP key with ret=%d", ret);
		return EC_ERROR_HW_INTERNAL;
	}

	if (bytes_are_trivial(otp_key, sizeof(otp_key))) {
		CPRINTS("ERROR: bytes read from OTP are trivial!");
		return EC_ERROR_HW_INTERNAL;
	}

	/*
	 * IKM is now the concatenation of the rollback secret, the seed
	 * from the TPM and the key stored in OTP
	 */
	memcpy(ikm.data() + IKM_OTP_OFFSET_BYTES, otp_key, sizeof(otp_key));
	BUILD_ASSERT((IKM_SIZE_BYTES - IKM_OTP_OFFSET_BYTES) ==
		     sizeof(otp_key));
	OPENSSL_cleanse(otp_key, OTP_KEY_SIZE_BYTES);
#endif

	return EC_SUCCESS;
}

test_mockable void compute_hmac_sha256(uint8_t *output, const uint8_t *key,
				       const int key_len,
				       const uint8_t *message,
				       const int message_len)
{
	hmac_SHA256(output, key, key_len, message, message_len);
}

static void hkdf_extract(uint8_t *prk, const uint8_t *salt, size_t salt_size,
			 const uint8_t *ikm, size_t ikm_size)
{
	/*
	 * Derive a key with the "extract" step of HKDF
	 * https://tools.ietf.org/html/rfc5869#section-2.2
	 */
	compute_hmac_sha256(prk, salt, salt_size, ikm, ikm_size);
}

static enum ec_error_list
hkdf_expand_one_step(uint8_t *out_key, size_t out_key_size, const uint8_t *prk,
		     size_t prk_size, const uint8_t *info, size_t info_size)
{
	uint8_t key_buf[SHA256_DIGEST_SIZE];
	uint8_t message_buf[SHA256_DIGEST_SIZE + 1];

	if (out_key_size > SHA256_DIGEST_SIZE) {
		CPRINTS("Deriving key material longer than SHA256_DIGEST_SIZE "
			"requires more steps of HKDF expand.");
		return EC_ERROR_INVAL;
	}

	if (info_size > SHA256_DIGEST_SIZE) {
		CPRINTS("Info size too big for HKDF.");
		return EC_ERROR_INVAL;
	}

	memcpy(message_buf, info, info_size);
	/* 1 step, set the counter byte to 1. */
	message_buf[info_size] = 0x01;
	compute_hmac_sha256(key_buf, prk, prk_size, message_buf, info_size + 1);

	memcpy(out_key, key_buf, out_key_size);
	OPENSSL_cleanse(key_buf, sizeof(key_buf));

	return EC_SUCCESS;
}

enum ec_error_list hkdf_expand(uint8_t *out_key, size_t L, const uint8_t *prk,
			       size_t prk_size, const uint8_t *info,
			       size_t info_size)
{
	/*
	 * "Expand" step of HKDF.
	 * https://tools.ietf.org/html/rfc5869#section-2.3
	 */
#define HASH_LEN SHA256_DIGEST_SIZE
	uint8_t count = 1;
	const uint8_t *T = out_key;
	size_t T_len = 0;
	uint8_t T_buffer[HASH_LEN];
	/* Number of blocks. */
	const uint32_t N = DIV_ROUND_UP(L, HASH_LEN);
	uint8_t info_buffer[HASH_LEN + HKDF_MAX_INFO_SIZE + sizeof(count)];
	bool arguments_valid = false;

	if (out_key == NULL || L == 0)
		CPRINTS("HKDF expand: output buffer not valid.");
	else if (prk == NULL)
		CPRINTS("HKDF expand: prk is NULL.");
	else if (info == NULL && info_size > 0)
		CPRINTS("HKDF expand: info is NULL but info size is not zero.");
	else if (info_size > HKDF_MAX_INFO_SIZE)
		CPRINTF("HKDF expand: info size larger than %d bytes.\n",
			HKDF_MAX_INFO_SIZE);
	else if (N > HKDF_SHA256_MAX_BLOCK_COUNT)
		CPRINTS("HKDF expand: output key size too large.");
	else
		arguments_valid = true;

	if (!arguments_valid)
		return EC_ERROR_INVAL;

	while (L > 0) {
		const size_t block_size = L < HASH_LEN ? L : HASH_LEN;

		memcpy(info_buffer, T, T_len);
		memcpy(info_buffer + T_len, info, info_size);
		info_buffer[T_len + info_size] = count;
		compute_hmac_sha256(T_buffer, prk, prk_size, info_buffer,
				    T_len + info_size + sizeof(count));
		memcpy(out_key, T_buffer, block_size);

		T += T_len;
		T_len = HASH_LEN;
		count++;
		out_key += block_size;
		L -= block_size;
	}
	OPENSSL_cleanse(T_buffer, sizeof(T_buffer));
	OPENSSL_cleanse(info_buffer, sizeof(info_buffer));
	return EC_SUCCESS;
#undef HASH_LEN
}

bool hkdf_sha256_impl(std::span<uint8_t> out_key, std::span<const uint8_t> ikm,
		      std::span<const uint8_t> salt,
		      std::span<const uint8_t> info)
{
	return HKDF(out_key.data(), out_key.size(), EVP_sha256(), ikm.data(),
		    ikm.size(), salt.data(), salt.size(), info.data(),
		    info.size());
}

test_mockable bool hkdf_sha256(std::span<uint8_t> out_key,
			       std::span<const uint8_t> ikm,
			       std::span<const uint8_t> salt,
			       std::span<const uint8_t> info)
{
	return hkdf_sha256_impl(out_key, ikm, salt, info);
}

enum ec_error_list
derive_positive_match_secret(std::span<uint8_t> output,
			     std::span<const uint8_t> input_positive_match_salt)
{
	enum ec_error_list ret;
	uint8_t ikm[IKM_SIZE_BYTES];
	uint8_t prk[SHA256_DIGEST_SIZE];
	static const char info_prefix[] = "positive_match_secret for user ";
	uint8_t info[sizeof(info_prefix) - 1 + sizeof(user_id)];

	if (bytes_are_trivial(input_positive_match_salt.data(),
			      input_positive_match_salt.size())) {
		CPRINTS("Failed to derive positive match secret: "
			"salt bytes are trivial.");
		return EC_ERROR_INVAL;
	}

	ret = get_ikm(ikm);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to get IKM: %d", ret);
		return ret;
	}

	/* "Extract" step of HKDF. */
	hkdf_extract(prk, input_positive_match_salt.data(),
		     input_positive_match_salt.size(), ikm, sizeof(ikm));
	OPENSSL_cleanse(ikm, sizeof(ikm));

	memcpy(info, info_prefix, strlen(info_prefix));
	memcpy(info + strlen(info_prefix), user_id, sizeof(user_id));

	/* "Expand" step of HKDF. */
	ret = hkdf_expand(output.data(), output.size(), prk, sizeof(prk), info,
			  sizeof(info));
	OPENSSL_cleanse(prk, sizeof(prk));

	/* Check that secret is not full of 0x00 or 0xff. */
	if (bytes_are_trivial(output.data(), output.size())) {
		CPRINTS("Failed to derive positive match secret: "
			"derived secret bytes are trivial.");
		ret = EC_ERROR_HW_INTERNAL;
	}
	return ret;
}

enum ec_error_list
derive_encryption_key_with_info(std::span<uint8_t> out_key,
				std::span<const uint8_t> salt,
				std::span<const uint8_t> info)
{
	enum ec_error_list ret;
	uint8_t ikm[IKM_SIZE_BYTES];
	uint8_t prk[SHA256_DIGEST_SIZE];

	BUILD_ASSERT(SBP_ENC_KEY_LEN <= SHA256_DIGEST_SIZE);
	BUILD_ASSERT(SBP_ENC_KEY_LEN <= CONFIG_ROLLBACK_SECRET_SIZE);

	if (info.size() != SHA256_DIGEST_SIZE) {
		CPRINTS("Invalid info size: %zu", info.size());
		return EC_ERROR_INVAL;
	}

	ret = get_ikm(ikm);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to get IKM: %d", ret);
		return ret;
	}

	/* TODO (b/276344630): Replace with boringssl version. */
	/* "Extract step of HKDF. */
	hkdf_extract(prk, salt.data(), salt.size(), ikm, sizeof(ikm));
	OPENSSL_cleanse(ikm, sizeof(ikm));

	/*
	 * Only 1 "expand" step of HKDF since the size of the "info" context
	 * (user_id in our case) is exactly SHA256_DIGEST_SIZE.
	 * https://tools.ietf.org/html/rfc5869#section-2.3
	 */
	ret = hkdf_expand_one_step(out_key.data(), out_key.size(), prk,
				   sizeof(prk), info.data(), info.size());
	OPENSSL_cleanse(prk, sizeof(prk));

	return ret;
}

enum ec_error_list derive_encryption_key(std::span<uint8_t> out_key,
					 std::span<const uint8_t> salt)
{
	BUILD_ASSERT(sizeof(user_id) == SHA256_DIGEST_SIZE);
	return derive_encryption_key_with_info(
		out_key, salt,
		{ reinterpret_cast<uint8_t *>(user_id), sizeof(user_id) });
}

enum ec_error_list aes_128_gcm_encrypt(std::span<const uint8_t> key,
				       std::span<const uint8_t> plaintext,
				       std::span<uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<uint8_t> tag)
{
	if (nonce.size() != FP_CONTEXT_NONCE_BYTES) {
		CPRINTS("Invalid nonce size %zu bytes", nonce.size());
		return EC_ERROR_INVAL;
	}

	bssl::ScopedEVP_AEAD_CTX ctx;
	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(),
				    key.data(), key.size(), tag.size(),
				    nullptr);
	if (!ret) {
		CPRINTS("Failed to initialize encryption context");
		return EC_ERROR_UNKNOWN;
	}

	size_t out_tag_size = 0;
	std::span<uint8_t> extra_input; /* no extra input */
	std::span<uint8_t> additional_data; /* no additional data */
	ret = EVP_AEAD_CTX_seal_scatter(
		ctx.get(), ciphertext.data(), tag.data(), &out_tag_size,
		tag.size(), nonce.data(), nonce.size(), plaintext.data(),
		plaintext.size(), extra_input.data(), extra_input.size(),
		additional_data.data(), additional_data.size());
	if (!ret) {
		CPRINTS("Failed to encrypt");
		return EC_ERROR_UNKNOWN;
	}
	if (out_tag_size != tag.size()) {
		CPRINTS("Resulting tag size %zu does not match expected size: %zu",
			out_tag_size, tag.size());
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

enum ec_error_list aes_128_gcm_decrypt(std::span<const uint8_t> key,
				       std::span<uint8_t> plaintext,
				       std::span<const uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<const uint8_t> tag)
{
	if (nonce.size() != FP_CONTEXT_NONCE_BYTES) {
		CPRINTS("Invalid nonce size %zu bytes", nonce.size());
		return EC_ERROR_INVAL;
	}

	bssl::ScopedEVP_AEAD_CTX ctx;
	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(),
				    key.data(), key.size(), tag.size(),
				    nullptr);
	if (!ret) {
		CPRINTS("Failed to initialize encryption context");
		return EC_ERROR_UNKNOWN;
	}

	std::span<uint8_t> additional_data; /* no additional data */
	ret = EVP_AEAD_CTX_open_gather(
		ctx.get(), plaintext.data(), nonce.data(), nonce.size(),
		ciphertext.data(), ciphertext.size(), tag.data(), tag.size(),
		additional_data.data(), additional_data.size());
	if (!ret) {
		CPRINTS("Failed to decrypt");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
