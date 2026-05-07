/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "openssl/mem.h"
#include "openssl/rand.h"
#include "scoped_fast_cpu.h"
#include "sha256.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#ifdef CONFIG_PLATFORM_EC_WP_EXTERNAL
#include <wp_external.h>
#endif

/* The session nonce for session key. */
static std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> session_nonce;

static std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key;

/* Current challenge */
static std::array<uint8_t, FP_CHALLENGE_SIZE> challenge;
test_export_static timestamp_t challenge_ctime;

bool fingerprint_auth_enabled()
{
	return global_context.fp_encryption_status &
	       FP_CONTEXT_STATUS_SESSION_ESTABLISHED;
}

__maybe_unused test_export_static void reset_session(void)
{
	OPENSSL_cleanse(session_nonce.data(), session_nonce.size());
	OPENSSL_cleanse(session_key.data(), session_key.size());
	OPENSSL_cleanse(global_context.tpm_seed.data(),
			global_context.tpm_seed.size());
	global_context.fp_encryption_status &= ~(
		FP_CONTEXT_SESSION_NONCE_SET |
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED | FP_ENC_STATUS_SEED_SET);
}

static enum ec_status
fp_command_generate_nonce(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_nonce *>(args->response);

	ScopedFastCpu fast_cpu;

	RAND_bytes(session_nonce.data(), session_nonce.size());

	std::ranges::copy(session_nonce, r->nonce);

	global_context.fp_encryption_status |= FP_CONTEXT_SESSION_NONCE_SET;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_NONCE, fp_command_generate_nonce,
		     EC_VER_MASK(0));

static enum ec_status
fp_command_establish_session(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_establish_session *>(
		args->params);
	static constexpr uint8_t tpm_seed_aad[] = { 't', 'p', 'm', '_',
						    's', 'e', 'e', 'd' };
	constexpr auto aad = std::span{ tpm_seed_aad };

	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_SESSION_NONCE_SET)) {
		CPRINTS("No existing session nonce");
		return EC_RES_ACCESS_DENIED;
	}

	/* Do not establish session if any operation on templates is running */
	if (global_context.sensor_mode & FP_MODES_TEMPLATE_OPERATION) {
		return EC_RES_BUSY;
	}

	ScopedFastCpu fast_cpu;

	/* Avoid using old TPM Seed if the seed decryption fails. */
	global_context.fp_encryption_status &= ~FP_ENC_STATUS_SEED_SET;
	OPENSSL_cleanse(global_context.tpm_seed.data(),
			global_context.tpm_seed.size());

	/* Invalidate the existing context and templates to prevent
	 * adding new templates unauthorized by TA.
	 */
	fp_reset_context();

	/* Reset SESSION_ESTABLISHED bit to avoid situation that the
	 * Fingerprint Auth is enabled with TPM Seed set using
	 * EC_CMD_FP_SEED_SET command (which is possible when the TPM Seed is
	 * not set, e.g. due to seed decryption error).
	 */
	global_context.fp_encryption_status &=
		~FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	enum ec_error_list ret = generate_session_key_with_context(
		session_nonce, p->peer_nonce, session_key);
	if (ret != EC_SUCCESS) {
		return EC_RES_INVALID_PARAM;
	}

	static_assert(sizeof(global_context.tpm_seed) ==
		      sizeof(p->enc_tpm_seed));
	CleanseWrapper<std::array<uint8_t, sizeof(global_context.tpm_seed)> >
		tpm_seed;

	ret = decrypt_data_with_session_key(session_key, p->enc_tpm_seed,
					    tpm_seed, p->nonce, p->tag, aad);
	if (ret != EC_SUCCESS) {
		return EC_RES_ERROR;
	}

	/* Set the TPM Seed. */
	std::ranges::copy(tpm_seed, global_context.tpm_seed.begin());
	global_context.fp_encryption_status |= FP_ENC_STATUS_SEED_SET;
	global_context.fp_encryption_status &= ~FP_CONTEXT_SESSION_NONCE_SET;
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_SESSION, fp_command_establish_session,
		     EC_VER_MASK(0));

static enum ec_status
fp_cmd_generate_challenge(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_challenge *>(
		args->response);

	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!fingerprint_auth_enabled()) {
		return EC_RES_ACCESS_DENIED;
	}

	ScopedFastCpu fast_cpu;

	RAND_bytes(challenge.data(), challenge.size());
	std::ranges::copy(challenge, r->challenge);

	timestamp_t now = get_time();
	challenge_ctime.val = now.val;

	global_context.fp_encryption_status |= FP_AUTH_CHALLENGE_SET;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_CHALLENGE, fp_cmd_generate_challenge,
		     EC_VER_MASK(0));

enum ec_error_list
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac)
{
	/* We expect the message to come from Fingerguard. */
	static constexpr uint8_t sender_str[] = {
		'f', 'i', 'n', 'g', 'e', 'r', '_', 'g', 'u', 'a', 'r', 'd'
	};
	static constexpr std::span sender = sender_str;
	std::array<uint8_t, SHA256_DIGEST_LENGTH> computed_mac{};

	/* Make sure new challenge was generated. */
	if (!(global_context.fp_encryption_status & FP_AUTH_CHALLENGE_SET)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Remove the bit so the challenge is not reused. */
	global_context.fp_encryption_status &= ~FP_AUTH_CHALLENGE_SET;

	/* Make sure the challenge has not expired. */
	timestamp_t now = get_time();
	if (now.val > challenge_ctime.val + (5 * SECOND)) {
		return EC_ERROR_TIMEOUT;
	}

	/* Compute expected signature. */
	if (compute_message_signature(session_key, context, sender, operation,
				      challenge, computed_mac) != EC_SUCCESS) {
		return EC_ERROR_INVAL;
	}

	/* Compare computed signature with received one. */
	static_assert(mac.size() == computed_mac.size());
	if (CRYPTO_memcmp(mac.data(), computed_mac.data(), mac.size())) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

enum ec_error_list
sign_message(std::span<const uint8_t> context,
	     std::span<const uint8_t> operation,
	     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge,
	     std::span<uint8_t, SHA256_DIGEST_LENGTH> output)
{
	static constexpr uint8_t sender_str[] = { 'f', 'p', 'm', 'c', 'u' };
	static constexpr std::span sender = sender_str;

	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!fingerprint_auth_enabled()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (compute_message_signature(session_key, context, sender, operation,
				      peer_challenge, output) != EC_SUCCESS) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_PLATFORM_EC_WP_EXTERNAL
static enum ec_status
fp_command_unlock_dev_options(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_unlock_dev_options *>(
		args->params);

	/* Define the empty context (no user_id) */
	static constexpr std::span<const uint8_t> context;

	static constexpr uint8_t operation_str[] = { 'e', 'n', 'a', 'b', 'l',
						     'e', '_', 'd', 'e', 'v',
						     '_', 'o', 'p', 't', 'i',
						     'o', 'n', 's' };
	static constexpr std::span operation = operation_str;

	if (validate_request(context, operation, p->hmac) != EC_SUCCESS) {
		return EC_RES_ACCESS_DENIED;
	}

	/* Unlock the system */
	disable_write_protect_external();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_UNLOCK_DEV_OPTIONS,
		     fp_command_unlock_dev_options, EC_VER_MASK(0));
#endif
