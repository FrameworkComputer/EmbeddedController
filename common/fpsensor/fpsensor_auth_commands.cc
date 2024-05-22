/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_template_state.h"
#include "openssl/mem.h"
#include "openssl/rand.h"
#include "scoped_fast_cpu.h"
#include "sha256.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>

/* The GSC pairing key. */
static std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

/* The auth nonce for GSC session key. */
std::array<uint8_t, FP_CK_AUTH_NONCE_LEN> auth_nonce;

enum ec_error_list check_context_cleared()
{
	for (uint8_t partial : global_context.user_id)
		if (partial != 0)
			return EC_ERROR_ACCESS_DENIED;
	for (uint8_t partial : auth_nonce)
		if (partial != 0)
			return EC_ERROR_ACCESS_DENIED;
	if (global_context.templ_valid != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.templ_dirty != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.positive_match_secret_state.template_matched !=
	    FP_NO_SUCH_TEMPLATE)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.fp_encryption_status & FP_CONTEXT_USER_ID_SET)
		return EC_ERROR_ACCESS_DENIED;
	return EC_SUCCESS;
}

static enum ec_status
fp_command_establish_pairing_key_keygen(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_establish_pairing_key_keygen *>(
		args->response);

	ScopedFastCpu fast_cpu;

	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();
	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	std::optional<fp_encrypted_private_key> encrypted_private_key =
		create_encrypted_private_key(*ecdh_key,
					     FP_AES_KEY_ENC_METADATA_VERSION,
					     global_context.user_id,
					     global_context.tpm_seed);
	if (!encrypted_private_key.has_value()) {
		CPRINTS("pairing_keygen: Failed to fill response encrypted private key");
		return EC_RES_UNAVAILABLE;
	}

	r->encrypted_private_key = std::move(encrypted_private_key).value();

	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);
	if (!pubkey.has_value()) {
		return EC_RES_UNAVAILABLE;
	}

	r->pubkey = std::move(pubkey).value();

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN,
		     fp_command_establish_pairing_key_keygen, EC_VER_MASK(0));

static enum ec_status
fp_command_establish_pairing_key_wrap(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_establish_pairing_key_wrap *>(
			args->params);
	auto *r = static_cast<ec_response_fp_establish_pairing_key_wrap *>(
		args->response);

	ScopedFastCpu fast_cpu;

	bssl::UniquePtr<EC_KEY> private_key = decrypt_private_key(
		params->encrypted_private_key, global_context.user_id,
		global_context.tpm_seed);
	if (private_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	bssl::UniquePtr<EC_KEY> public_key =
		create_ec_key_from_pubkey(params->peers_pubkey);
	if (public_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	enum ec_error_list ret = generate_ecdh_shared_secret(
		*private_key, *public_key, r->encrypted_pairing_key.data,
		sizeof(r->encrypted_pairing_key.data));
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	ret = encrypt_data_in_place(FP_AES_KEY_ENC_METADATA_VERSION,
				    r->encrypted_pairing_key.info,
				    global_context.user_id,
				    global_context.tpm_seed,
				    r->encrypted_pairing_key.data);
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP,
		     fp_command_establish_pairing_key_wrap, EC_VER_MASK(0));

static enum ec_status
fp_command_load_pairing_key(struct host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_load_pairing_key *>(
		args->params);

	ScopedFastCpu fast_cpu;

	/* If the context is not cleared, reject this request to prevent leaking
	 * the existing template. */
	enum ec_error_list ret = check_context_cleared();
	if (ret != EC_SUCCESS) {
		CPRINTS("load_pairing_key: Context is not clean");
		return EC_RES_ACCESS_DENIED;
	}

	if (global_context.fp_encryption_status &
	    FP_CONTEXT_STATUS_NONCE_CONTEXT_SET) {
		CPRINTS("load_pairing_key: In an nonce context");
		return EC_RES_ACCESS_DENIED;
	}

	ret = decrypt_data(params->encrypted_pairing_key.info,
			   global_context.user_id, global_context.tpm_seed,
			   params->encrypted_pairing_key.data, pairing_key);
	if (ret != EC_SUCCESS) {
		CPRINTS("load_pairing_key: Failed to decrypt pairing key");
		return EC_RES_UNAVAILABLE;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_LOAD_PAIRING_KEY, fp_command_load_pairing_key,
		     EC_VER_MASK(0));

static enum ec_status
fp_command_generate_nonce(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_nonce *>(args->response);

	ScopedFastCpu fast_cpu;

	if (global_context.fp_encryption_status &
	    FP_CONTEXT_STATUS_NONCE_CONTEXT_SET) {
		/* Invalidate the existing context and templates to prevent
		 * leaking the existing template. */
		fp_reset_context();
	}

	RAND_bytes(auth_nonce.data(), auth_nonce.size());

	std::ranges::copy(auth_nonce, r->nonce);

	global_context.fp_encryption_status |= FP_CONTEXT_AUTH_NONCE_SET;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_NONCE, fp_command_generate_nonce,
		     EC_VER_MASK(0));

static enum ec_status
fp_command_nonce_context(struct host_cmd_handler_args *args)
{
	const auto *p =
		static_cast<const ec_params_fp_nonce_context *>(args->params);

	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_AUTH_NONCE_SET)) {
		CPRINTS("No existing auth nonce");
		return EC_RES_ACCESS_DENIED;
	}

	ScopedFastCpu fast_cpu;

	std::array<uint8_t, SHA256_DIGEST_SIZE> gsc_session_key;
	enum ec_error_list ret = generate_gsc_session_key(
		auth_nonce, p->gsc_nonce, pairing_key, gsc_session_key);

	if (ret != EC_SUCCESS) {
		return EC_RES_INVALID_PARAM;
	}

	static_assert(sizeof(global_context.user_id) == sizeof(p->enc_user_id));
	std::array<uint8_t, sizeof(global_context.user_id)> raw_user_id;
	std::ranges::copy(p->enc_user_id, raw_user_id.begin());

	ret = decrypt_data_with_gsc_session_key_in_place(
		gsc_session_key, p->enc_user_id_iv, raw_user_id);

	if (ret != EC_SUCCESS) {
		return EC_RES_ERROR;
	}

	/* Set the user_id. */
	std::copy(raw_user_id.begin(), raw_user_id.end(),
		  global_context.user_id);

	global_context.fp_encryption_status &= FP_ENC_STATUS_SEED_SET;
	global_context.fp_encryption_status |= FP_CONTEXT_USER_ID_SET;
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_NONCE_CONTEXT_SET;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_NONCE_CONTEXT, fp_command_nonce_context,
		     EC_VER_MASK(0));

static enum ec_status
fp_command_read_match_secret_with_pubkey(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_read_match_secret_with_pubkey *>(
			args->params);
	auto *response =
		static_cast<ec_response_fp_read_match_secret_with_pubkey *>(
			args->response);
	int8_t fgr = params->fgr;

	ScopedFastCpu fast_cpu;

	static_assert(sizeof(response->enc_secret) ==
		      FP_POSITIVE_MATCH_SECRET_BYTES);

	CleanseWrapper<std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >
		secret;

	enum ec_status status = fp_read_match_secret(fgr, secret);
	if (status != EC_RES_SUCCESS) {
		return status;
	}

	enum ec_error_list ret = encrypt_data_with_ecdh_key_in_place(
		params->pubkey, secret, response->iv, response->pubkey);

	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	std::ranges::copy(secret, response->enc_secret);

	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_READ_MATCH_SECRET_WITH_PUBKEY,
		     fp_command_read_match_secret_with_pubkey, EC_VER_MASK(0));

static enum ec_status unlock_template(uint16_t idx)
{
	auto *dec_state = std::get_if<fp_decrypted_template_state>(
		&global_context.template_states[idx]);
	if (dec_state) {
		if (safe_memcmp(dec_state->user_id.begin(),
				global_context.user_id,
				sizeof(global_context.user_id)) != 0) {
			return EC_RES_ACCESS_DENIED;
		}
		return EC_RES_SUCCESS;
	}

	auto *enc_state = std::get_if<fp_encrypted_template_state>(
		&global_context.template_states[idx]);
	if (!enc_state) {
		return EC_RES_INVALID_PARAM;
	}

	ec_fp_template_encryption_metadata &enc_info = enc_state->enc_metadata;
	if (enc_info.struct_version != 4) {
		return EC_RES_ACCESS_DENIED;
	}

	/* We reuse the fp_enc_buffer for the data decryption, because we don't
	 * want to allocate a huge array on the stack.
	 * Note: fp_enc_buffer = fp_template || fp_positive_match_salt */
	constexpr std::span enc_template = fp_enc_buffer.fp_template;
	constexpr std::span enc_salt = fp_enc_buffer.positive_match_salt;
	constexpr std::span enc_buffer(enc_template.data(),
				       enc_template.size() + enc_salt.size());
	static_assert(enc_buffer.size() <= sizeof(fp_enc_buffer));

	std::ranges::copy(fp_template[idx], enc_template.begin());
	std::ranges::copy(global_context.fp_positive_match_salt[idx],
			  enc_salt.begin());

	FpEncryptionKey key;
	if (derive_encryption_key(key, enc_info.encryption_salt,
				  global_context.user_id,
				  global_context.tpm_seed) != EC_SUCCESS) {
		fp_clear_finger_context(idx);
		OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));
		return EC_RES_UNAVAILABLE;
	}

	if (aes_128_gcm_decrypt(key, enc_buffer, enc_buffer, enc_info.nonce,
				enc_info.tag) != EC_SUCCESS) {
		fp_clear_finger_context(idx);
		OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));
		return EC_RES_UNAVAILABLE;
	}

	std::ranges::copy(enc_template, fp_template[idx]);
	std::ranges::copy(enc_salt, global_context.fp_positive_match_salt[idx]);

	fp_init_decrypted_template_state_with_user_id(idx);
	OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));
	return EC_RES_SUCCESS;
}

static enum ec_status
fp_command_unlock_template(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_unlock_template *>(args->params);
	uint16_t fgr_num = params->fgr_num;

	ScopedFastCpu fast_cpu;

	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_STATUS_NONCE_CONTEXT_SET)) {
		return EC_RES_ACCESS_DENIED;
	}

	if (global_context.fp_encryption_status &
	    FP_CONTEXT_STATUS_MATCH_PROCESSED_SET) {
		return EC_RES_ACCESS_DENIED;
	}

	if (fgr_num > global_context.template_states.size()) {
		return EC_RES_OVERFLOW;
	}

	for (uint16_t idx = 0; idx < fgr_num; idx++) {
		enum ec_status res = unlock_template(idx);
		if (res != EC_RES_SUCCESS) {
			return res;
		}
	}

	global_context.fp_encryption_status |= FP_CONTEXT_TEMPLATE_UNLOCKED_SET;
	global_context.templ_valid = fgr_num;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_UNLOCK_TEMPLATE, fp_command_unlock_template,
		     EC_VER_MASK(0));
