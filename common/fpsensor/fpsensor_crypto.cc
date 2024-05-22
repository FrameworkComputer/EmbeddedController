/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "openssl/aead.h"
#include "openssl/evp.h"
#include "openssl/hkdf.h"
#include "openssl/mem.h"
#include "otp_key.h"
#include "rollback.h"
#include "sha256.h"
#include "util.h"

#include <stdbool.h>

#include <span>

#ifdef CONFIG_OTP_KEY
constexpr uint8_t IKM_OTP_OFFSET_BYTES =
	CONFIG_ROLLBACK_SECRET_SIZE + FP_CONTEXT_TPM_BYTES;
constexpr uint8_t IKM_SIZE_BYTES = IKM_OTP_OFFSET_BYTES + OTP_KEY_SIZE_BYTES;
BUILD_ASSERT(IKM_SIZE_BYTES == 96);

#else
constexpr uint8_t IKM_SIZE_BYTES =
	CONFIG_ROLLBACK_SECRET_SIZE + FP_CONTEXT_TPM_BYTES;
BUILD_ASSERT(IKM_SIZE_BYTES == 64);
#endif

#if !defined(CONFIG_BORINGSSL_CRYPTO) || !defined(CONFIG_ROLLBACK_SECRET_SIZE)
#error "fpsensor requires CONFIG_BORINGSSL_CRYPTO and ROLLBACK_SECRET_SIZE"
#endif

test_export_static enum ec_error_list
get_ikm(std::span<uint8_t, IKM_SIZE_BYTES> ikm,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	enum ec_error_list ret;

	if (bytes_are_trivial(tpm_seed.data(), tpm_seed.size_bytes())) {
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
	memcpy(ikm.data() + CONFIG_ROLLBACK_SECRET_SIZE, tpm_seed.data(),
	       tpm_seed.size_bytes());

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

enum ec_error_list derive_positive_match_secret(
	std::span<uint8_t> output,
	std::span<const uint8_t> input_positive_match_salt,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	enum ec_error_list ret;
	CleanseWrapper<std::array<uint8_t, IKM_SIZE_BYTES> > ikm;
	static const char info_prefix[] = "positive_match_secret for user ";
	uint8_t info[sizeof(info_prefix) - 1 + user_id.size_bytes()];

	if (bytes_are_trivial(input_positive_match_salt.data(),
			      input_positive_match_salt.size())) {
		CPRINTS("Failed to derive positive match secret: "
			"salt bytes are trivial.");
		return EC_ERROR_INVAL;
	}

	ret = get_ikm(ikm, tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to get IKM: %d", ret);
		return ret;
	}

	memcpy(info, info_prefix, strlen(info_prefix));
	memcpy(info + strlen(info_prefix), user_id.data(),
	       user_id.size_bytes());

	if (!hkdf_sha256(output, ikm, input_positive_match_salt, info)) {
		CPRINTS("Failed to perform HKDF");
		return EC_ERROR_UNKNOWN;
	}

	/* Check that secret is not full of 0x00 or 0xff. */
	if (bytes_are_trivial(output.data(), output.size())) {
		CPRINTS("Failed to derive positive match secret: "
			"derived secret bytes are trivial.");
		ret = EC_ERROR_HW_INTERNAL;
	}
	return ret;
}

enum ec_error_list
derive_encryption_key(std::span<uint8_t> out_key, std::span<const uint8_t> salt,
		      std::span<const uint8_t> info,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	enum ec_error_list ret;
	CleanseWrapper<std::array<uint8_t, IKM_SIZE_BYTES> > ikm;

	if (info.size() != SHA256_DIGEST_SIZE) {
		CPRINTS("Invalid info size: %zu", info.size());
		return EC_ERROR_INVAL;
	}

	ret = get_ikm(ikm, tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to get IKM: %d", ret);
		return ret;
	}

	if (!hkdf_sha256(out_key, ikm, salt, info)) {
		CPRINTS("Failed to perform HKDF");
		return EC_ERROR_UNKNOWN;
	}

	return ret;
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
