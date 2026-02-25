/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_state.h"
#include "host_command.h"
#include "scoped_fast_cpu.h"

/* Pointer to the FPMCU's ECDH private key */
static bssl::UniquePtr<EC_KEY> ecdh_key;

/* The FPMCU pairing key. */
static std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

enum ec_error_list check_context_cleared()
{
	for (uint8_t partial : global_context.user_id)
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

	ecdh_key = generate_elliptic_curve_key();
	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);
	if (!pubkey.has_value()) {
		return EC_RES_UNAVAILABLE;
	}

	r->pubkey = pubkey.value();

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

	CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> > new_pairing_key;

	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	ScopedFastCpu fast_cpu;

	bssl::UniquePtr<EC_KEY> public_key =
		create_ec_key_from_pubkey(params->peers_pubkey);
	if (public_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	/*
	 * The Pairing Key is only used to produce the Session Key.
	 * It's not used as a key for symmetric encryption. It's okay
	 * to not apply KDF in this case.
	 */
	enum ec_error_list ret = generate_ecdh_shared_secret_without_kdf(
		*ecdh_key, *public_key, new_pairing_key);
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	ret = encrypt_pairing_key(FP_AES_KEY_ENC_METADATA_VERSION,
				  r->encrypted_pairing_key.info,
				  new_pairing_key,
				  r->encrypted_pairing_key.data);
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	/* Deallocate the FPMCU's ECDH private key. */
	ecdh_key = nullptr;

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

	enum ec_error_list ret = decrypt_pairing_key(
		params->encrypted_pairing_key.info,
		params->encrypted_pairing_key.data, pairing_key);
	if (ret != EC_SUCCESS) {
		CPRINTS("load_pairing_key: Failed to decrypt pairing key");
		return EC_RES_UNAVAILABLE;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_LOAD_PAIRING_KEY, fp_command_load_pairing_key,
		     EC_VER_MASK(0));

enum ec_error_list generate_session_key_with_context(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key)
{
	return generate_session_key(fpmcu_nonce, peer_nonce, pairing_key, {},
				    session_key);
}
