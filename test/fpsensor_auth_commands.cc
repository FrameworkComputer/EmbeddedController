/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "mock/fpsensor_state_mock.h"
#include "mock/otpi_mock.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/evp.h"
#include "openssl/obj_mac.h"
#include "sha256.h"
#include "test_util.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <span>
#include <variant>

/* Function used to cleanup session secrets and flags. */
void reset_session(void);

/* Challenge creation time. */
extern timestamp_t challenge_ctime;

namespace
{

static enum ec_error_list get_fp_encryption_status(uint32_t *status)
{
	struct ec_response_fp_encryption_status resp = { 0 };

	TEST_EQ(test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				       sizeof(resp)),
		EC_RES_SUCCESS, "%d");
	*status = resp.status;

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_pairing_key_keygen(void)
{
	enum ec_status rv;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	bssl::UniquePtr<EC_KEY> pubkey =
		create_ec_key_from_pubkey(keygen_response.pubkey);

	TEST_NE(pubkey.get(), nullptr, "%p");
	TEST_EQ(EC_KEY_check_key(pubkey.get()), 1, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_and_load_pairing_key(void)
{
	enum ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response;
	ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_establish_pairing_key_fail(void)
{
	enum ec_status rv;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	struct ec_response_fp_establish_pairing_key_wrap wrap_response;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_NE(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_load_pairing_key_invalid(void)
{
	enum ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response;
	ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* No encryption info. */
	memset(&load_params.encrypted_pairing_key.info, 0,
	       sizeof(load_params.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_UNAVAILABLE, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_generate_nonce(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

static enum ec_error_list
initialize_pairing_key(std::span<uint8_t, FP_PAIRING_KEY_LEN> pairing_key)
{
	enum ec_status rv;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params;
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	/* Ask FPMCU for its public key */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));
	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Convert FPMCU public key to more useful form */
	bssl::UniquePtr<EC_KEY> fpmcu_public_key =
		create_ec_key_from_pubkey(keygen_response.pubkey);
	TEST_NE(fpmcu_public_key.get(), nullptr, "%p");

	/* Generate ECDH private key on our side */
	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();
	TEST_NE(ecdh_key.get(), nullptr, "%p");

	/* Get public key from that key */
	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);
	TEST_ASSERT(pubkey.has_value());

	wrap_params.peers_pubkey = pubkey.value();

	/* Generate Pairing Key on our side */
	enum ec_error_list ret = generate_ecdh_shared_secret_without_kdf(
		*ecdh_key, *fpmcu_public_key, pairing_key);
	TEST_EQ(ret, EC_SUCCESS, "%d");

	/*
	 * Send our public key to the FPMCU. FPMCU will return encrypted
	 * Pairing Key.
	 */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));
	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	load_params.encrypted_pairing_key = wrap_response.encrypted_pairing_key;

	/* Load Pairing Key to the FPMCU */
	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), nullptr, 0);
	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

static enum ec_error_list generate_valid_establish_session_request(
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	struct ec_params_fp_establish_session *session_params)
{
	static constexpr uint8_t tpm_seed_aad[] = { 't', 'p', 'm', '_',
						    's', 'e', 'e', 'd' };
	constexpr auto aad = std::span{ tpm_seed_aad };

	/* Get our session nonce */
	std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> session_nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	/* Obtain session key on our side */
	std::array<uint8_t, SHA256_DIGEST_SIZE> session_key;
	enum ec_error_list ret = generate_session_key(
		fpmcu_nonce, session_nonce, pairing_key, {}, session_key);
	TEST_EQ(ret, EC_SUCCESS, "%d");

	TEST_EQ(tpm_seed.size(), sizeof(session_params->enc_tpm_seed), "%zu");

	std::array<uint8_t, FP_AES_KEY_NONCE_BYTES> nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};
	TEST_EQ(nonce.size(), sizeof(session_params->nonce), "%zu");
	memcpy(session_params->nonce, nonce.data(), FP_AES_KEY_NONCE_BYTES);

	/* Encrypt tpm_seed using session key */
	bssl::ScopedEVP_AEAD_CTX ctx;
	int aead_ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(),
					 session_key.data(), session_key.size(),
					 sizeof(session_params->tag), nullptr);
	TEST_EQ(aead_ret, 1, "%d");

	size_t tag_bytes_written = 0;
	aead_ret = EVP_AEAD_CTX_seal_scatter(
		ctx.get(), session_params->enc_tpm_seed, session_params->tag,
		&tag_bytes_written, sizeof(session_params->tag),
		session_params->nonce, sizeof(session_params->nonce),
		tpm_seed.data(), tpm_seed.size(), /* extra_in = */ nullptr,
		/* extra_in_len = */ 0, aad.data(), aad.size());
	TEST_EQ(aead_ret, 1, "%d");
	TEST_EQ(tag_bytes_written, sizeof(session_params->tag), "%zu");

	/* Copy our session nonce to the structure */
	std::ranges::copy(session_nonce, session_params->peer_nonce);

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_establish_session(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_CLEARED((int)status, FP_ENC_STATUS_SEED_SET);

	global_context.templ_valid = 1;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_CLEARED((int)status, FP_ENC_STATUS_SEED_SET);

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)status, FP_ENC_STATUS_SEED_SET);

	TEST_EQ(global_context.templ_valid, 0u, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_fail_different_pk(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	// Change Pairing Key to different one.
	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	// Try to establish session using request prepared for previous
	// Pairing Key.
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	// Expect failure in TPM Seed decryption.
	TEST_EQ(rv, EC_RES_ERROR, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_establish_session_deny(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	// Establish session without generate nonce should fail.
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_limit_without_generated_nonce(void)
{
	enum ec_status rv;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	/* Call nonce context without generated nonce should fail. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_limit_normal_context(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
	};
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Normal context should not clear the generated nonce. */
	/* This will be used for the migration path. */
	rv = test_send_host_command(EC_CMD_FP_CONTEXT, 1, &ctx_params,
				    sizeof(ctx_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	/* Call nonce context with generated nonce should success. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_limit_twice_1(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Call nonce context twice should fail. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_limit_twice_2(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Call nonce context twice should fail even we generated two nonces. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_establish_session_load_pk(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Pairing key can be loaded after the session was established. */
	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_template_decrypted(void)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params = {};

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       NULL, 0),
		EC_RES_SUCCESS, "%d");

	constexpr size_t head_size = offsetof(ec_params_fp_template, data);
	constexpr size_t metadata_size =
		sizeof(ec_fp_template_encryption_metadata);
	constexpr size_t template_size = sizeof(fp_template[0]);
	constexpr size_t salt_size =
		sizeof(global_context.fp_positive_match_salt[0]);
	constexpr size_t params_size =
		head_size + metadata_size + template_size + salt_size;

	std::array<uint8_t, params_size> params = {};
	std::span head(params.begin(), head_size);
	std::span enc_metadata(head.end(), metadata_size);
	std::span template_data(enc_metadata.end(), template_size);
	std::span salt_data(template_data.end(), salt_size);

	struct ec_params_fp_template head_data = {
		.offset = 0,
		.size = FP_TEMPLATE_COMMIT | (params_size - head_size),
	};
	static_assert(head_size == sizeof(head_data));
	memcpy(head.data(), &head_data, head.size());

	std::ranges::fill(template_data, 0xc4);
	std::ranges::fill(salt_data, 0xab);

	struct fp_auth_command_encryption_metadata info;
	encrypt_data_in_place(1, info, global_context.user_id,
			      global_context.tpm_seed,
			      { template_data.data(),
				template_data.size() + salt_data.size() });

	struct ec_fp_template_encryption_metadata enc_metadata_data{
		.struct_version = 4
	};

	static_assert(sizeof(info.nonce) == sizeof(enc_metadata_data.nonce));
	std::ranges::copy(info.nonce, enc_metadata_data.nonce);

	static_assert(sizeof(info.encryption_salt) ==
		      sizeof(enc_metadata_data.encryption_salt));
	std::ranges::copy(info.encryption_salt,
			  enc_metadata_data.encryption_salt);

	static_assert(sizeof(info.tag) == sizeof(enc_metadata_data.tag));
	std::ranges::copy(info.tag, enc_metadata_data.tag);

	static_assert(metadata_size == sizeof(enc_metadata_data));
	memcpy(enc_metadata.data(), &enc_metadata_data, enc_metadata.size());

	TEST_EQ(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, params.data(),
				       params.size(), NULL, 0),
		EC_RES_SUCCESS, "%d");

	uint32_t status;
	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

// Test that legacy format (v3) isn't accepted by commit function.
test_static enum ec_error_list test_fp_command_commit_v3(void)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params = {};

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       NULL, 0),
		EC_RES_SUCCESS, "%d");

	constexpr size_t head_size = offsetof(ec_params_fp_template, data);
	constexpr size_t metadata_size =
		sizeof(ec_fp_template_encryption_metadata);
	constexpr size_t template_size = sizeof(fp_template[0]);
	constexpr size_t params_size =
		head_size + metadata_size + template_size;

	std::array<uint8_t, params_size> params = {};
	std::span head(params.begin(), head_size);
	std::span enc_metadata(head.end(), metadata_size);
	std::span template_data(enc_metadata.end(), template_size);

	struct ec_params_fp_template head_data = {
		.offset = 0,
		.size = FP_TEMPLATE_COMMIT | (params_size - head_size),
	};
	static_assert(head_size == sizeof(head_data));
	memcpy(head.data(), &head_data, head.size());

	std::ranges::fill(template_data, 0xc4);

	struct fp_auth_command_encryption_metadata info;
	encrypt_data_in_place(1, info, global_context.user_id,
			      global_context.tpm_seed, template_data);

	struct ec_fp_template_encryption_metadata enc_metadata_data{
		.struct_version = 3
	};

	static_assert(sizeof(info.nonce) == sizeof(enc_metadata_data.nonce));
	std::ranges::copy(info.nonce, enc_metadata_data.nonce);

	static_assert(sizeof(info.encryption_salt) ==
		      sizeof(enc_metadata_data.encryption_salt));
	std::ranges::copy(info.encryption_salt,
			  enc_metadata_data.encryption_salt);

	static_assert(sizeof(info.tag) == sizeof(enc_metadata_data.tag));
	std::ranges::copy(info.tag, enc_metadata_data.tag);

	static_assert(metadata_size == sizeof(enc_metadata_data));
	memcpy(enc_metadata.data(), &enc_metadata_data, enc_metadata.size());

	TEST_EQ(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, params.data(),
				       params.size(), NULL, 0),
		EC_RES_INVALID_PARAM, "%d");

	return EC_SUCCESS;
}

// Test that trivial positive match salt will be detected into an error.
test_static enum ec_error_list test_fp_command_commit_trivial_salt(void)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params = {};

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       NULL, 0),
		EC_RES_SUCCESS, "%d");

	// Templates will only be decrypted in active context.
	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 0, 1, 2, 3, 4, 5, 6, 7 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &ctx_params,
				       sizeof(ctx_params), NULL, 0),
		EC_RES_SUCCESS, "%d");

	constexpr size_t head_size = offsetof(ec_params_fp_template, data);
	constexpr size_t metadata_size =
		sizeof(ec_fp_template_encryption_metadata);
	constexpr size_t template_size = sizeof(fp_template[0]);
	constexpr size_t salt_size =
		sizeof(global_context.fp_positive_match_salt[0]);
	constexpr size_t params_size =
		head_size + metadata_size + template_size + salt_size;

	std::array<uint8_t, params_size> params = {};
	std::span head(params.begin(), head_size);
	std::span enc_metadata(head.end(), metadata_size);
	std::span template_data(enc_metadata.end(), template_size);
	std::span salt_data(template_data.end(), salt_size);

	struct ec_params_fp_template head_data = {
		.offset = 0,
		.size = FP_TEMPLATE_COMMIT | (params_size - head_size),
	};
	static_assert(head_size == sizeof(head_data));
	memcpy(head.data(), &head_data, head.size());

	std::ranges::fill(template_data, 0xc4);

	struct fp_auth_command_encryption_metadata info;
	encrypt_data_in_place(1, info, global_context.user_id,
			      global_context.tpm_seed,
			      { template_data.data(),
				template_data.size() + salt_data.size() });

	struct ec_fp_template_encryption_metadata enc_metadata_data{
		.struct_version = 4
	};

	static_assert(sizeof(info.nonce) == sizeof(enc_metadata_data.nonce));
	std::ranges::copy(info.nonce, enc_metadata_data.nonce);

	static_assert(sizeof(info.encryption_salt) ==
		      sizeof(enc_metadata_data.encryption_salt));
	std::ranges::copy(info.encryption_salt,
			  enc_metadata_data.encryption_salt);

	static_assert(sizeof(info.tag) == sizeof(enc_metadata_data.tag));
	std::ranges::copy(info.tag, enc_metadata_data.tag);

	static_assert(metadata_size == sizeof(enc_metadata_data));
	memcpy(enc_metadata.data(), &enc_metadata_data, enc_metadata.size());

	TEST_EQ(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, params.data(),
				       params.size(), NULL, 0),
		EC_RES_INVALID_PARAM, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_commit_without_seed(void)
{
	fp_reset_and_clear_context();

	// Templates will only be decrypted in active context.
	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 0, 1, 2, 3, 4, 5, 6, 7 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &ctx_params,
				       sizeof(ctx_params), NULL, 0),
		EC_RES_SUCCESS, "%d");

	constexpr size_t head_size = offsetof(ec_params_fp_template, data);
	constexpr size_t metadata_size =
		sizeof(ec_fp_template_encryption_metadata);
	constexpr size_t template_size = sizeof(fp_template[0]);
	constexpr size_t salt_size =
		sizeof(global_context.fp_positive_match_salt[0]);
	constexpr size_t params_size =
		head_size + metadata_size + template_size + salt_size;

	std::array<uint8_t, params_size> params = {};
	std::span head(params.begin(), head_size);
	std::span enc_metadata(head.end(), metadata_size);
	std::span template_data(enc_metadata.end(), template_size);
	std::span salt_data(template_data.end(), salt_size);

	struct ec_params_fp_template head_data = {
		.offset = 0,
		.size = FP_TEMPLATE_COMMIT | (params_size - head_size),
	};
	static_assert(head_size == sizeof(head_data));
	memcpy(head.data(), &head_data, head.size());

	std::ranges::fill(template_data, 0xc4);
	std::ranges::fill(salt_data, 0x12);

	struct fp_auth_command_encryption_metadata info;

	struct ec_fp_template_encryption_metadata enc_metadata_data{
		.struct_version = 4
	};

	static_assert(sizeof(info.nonce) == sizeof(enc_metadata_data.nonce));
	std::ranges::copy(info.nonce, enc_metadata_data.nonce);

	static_assert(sizeof(info.encryption_salt) ==
		      sizeof(enc_metadata_data.encryption_salt));
	std::ranges::copy(info.encryption_salt,
			  enc_metadata_data.encryption_salt);

	static_assert(sizeof(info.tag) == sizeof(enc_metadata_data.tag));
	std::ranges::copy(info.tag, enc_metadata_data.tag);

	static_assert(metadata_size == sizeof(enc_metadata_data));
	memcpy(enc_metadata.data(), &enc_metadata_data, enc_metadata.size());

	TEST_EQ(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, params.data(),
				       params.size(), NULL, 0),
		EC_RES_UNAVAILABLE, "%d");

	return EC_SUCCESS;
}

static enum ec_error_list
establish_session(std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key{};
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	struct ec_response_fp_generate_nonce nonce_response{};
	struct ec_params_fp_establish_session session_params{};

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, nullptr, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	/*
	 * This is the same nonce as the one hardcoded in
	 * generate_valid_establish_session_request.
	 */
	constexpr std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	TEST_EQ(generate_session_key(nonce_response.nonce, peer_nonce,
				     pairing_key, {}, session_key),
		EC_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       nullptr, 0),
		EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_generate_challenge(void)
{
	struct ec_response_fp_generate_challenge challenge_response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	uint32_t status;

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_CLEARED((int)status, FP_AUTH_CHALLENGE_SET);

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)status, FP_AUTH_CHALLENGE_SET);

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_generate_challenge_fail_no_session(void)
{
	struct ec_response_fp_generate_challenge challenge_response{};

	fp_reset_and_clear_context();
	reset_session();

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_validate_request(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	constexpr std::array<const uint8_t, 12> sender = { 'f', 'i', 'n', 'g',
							   'e', 'r', '_', 'g',
							   'u', 'a', 'r', 'd' };

	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	struct ec_response_fp_generate_challenge challenge_response{};
	uint32_t status;

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)status, FP_AUTH_CHALLENGE_SET);

	TEST_EQ(compute_message_signature(session_key, user_id, sender,
					  operation,
					  challenge_response.challenge, mac),
		EC_SUCCESS, "%d");

	TEST_EQ(validate_request(user_id, operation, mac), EC_SUCCESS, "%d");

	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_CLEARED((int)status, FP_AUTH_CHALLENGE_SET);

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_validate_request_fail_no_challenge(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac = { 0 };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	TEST_EQ(validate_request(user_id, operation, mac),
		EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_validate_request_fail_invalid_signature(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac = { 0 };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(validate_request(user_id, operation, mac),
		EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_validate_request_fail_timeout(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	constexpr std::array<const uint8_t, 12> sender = { 'f', 'i', 'n', 'g',
							   'e', 'r', '_', 'g',
							   'u', 'a', 'r', 'd' };

	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	struct ec_response_fp_generate_challenge challenge_response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	challenge_ctime.val -= 6 * SECOND;

	TEST_EQ(compute_message_signature(session_key, user_id, sender,
					  operation,
					  challenge_response.challenge, mac),
		EC_SUCCESS, "%d");

	TEST_EQ(validate_request(user_id, operation, mac), EC_ERROR_TIMEOUT,
		"%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_sign_message(void)
{
	constexpr std::array<const uint8_t, FP_CHALLENGE_SIZE> challenge = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5,
	};
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 5> sender = { 'f', 'p', 'm', 'c',
							  'u' };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };

	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_mac{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	TEST_EQ(sign_message(user_id, operation, challenge, mac), EC_SUCCESS,
		"%d");

	TEST_EQ(compute_message_signature(session_key, user_id, sender,
					  operation, challenge, expected_mac),
		EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(mac, expected_mac, mac.size());

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_sign_message_fail_no_session(void)
{
	constexpr std::array<const uint8_t, FP_CHALLENGE_SIZE> challenge = { 0 };
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(sign_message(user_id, operation, challenge, mac),
		EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_match_correct_signature(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 4> operation = { 'a', 'u', 't',
							     'h' };
	constexpr std::array<const uint8_t, 12> sender = { 'f', 'i', 'n', 'g',
							   'e', 'r', '_', 'g',
							   'u', 'a', 'r', 'd' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_MATCH };
	struct ec_response_fp_mode response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(compute_message_signature(
			session_key, user_id, sender, operation,
			challenge_response.challenge, params.mac),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, FP_MODE_MATCH, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_disable_match_no_signature(void)
{
	struct ec_params_fp_mode_v1 params = { .mode = 0 };
	struct ec_response_fp_mode response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	global_context.sensor_mode = FP_MODE_MATCH;

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, 0u, "%x");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_match_invalid_signature(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_MATCH };
	struct ec_response_fp_mode response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_enroll_correct_signature(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	constexpr std::array<const uint8_t, 12> sender = { 'f', 'i', 'n', 'g',
							   'e', 'r', '_', 'g',
							   'u', 'a', 'r', 'd' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						       FP_MODE_ENROLL_IMAGE };
	struct ec_response_fp_mode response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(compute_message_signature(
			session_key, user_id, sender, operation,
			challenge_response.challenge, params.mac),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
		"%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_disable_enroll_no_signature(void)
{
	struct ec_params_fp_mode_v1 params = { .mode = 0 };
	struct ec_response_fp_mode response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, 0u, "%x");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_enroll_invalid_signature(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						       FP_MODE_ENROLL_IMAGE };
	struct ec_response_fp_mode response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_enroll_session_transitions(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 6> operation = { 'e', 'n', 'r',
							     'o', 'l', 'l' };
	constexpr std::array<const uint8_t, 12> sender = { 'f', 'i', 'n', 'g',
							   'e', 'r', '_', 'g',
							   'u', 'a', 'r', 'd' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						       FP_MODE_ENROLL_IMAGE };
	struct ec_response_fp_mode response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(compute_message_signature(
			session_key, user_id, sender, operation,
			challenge_response.challenge, params.mac),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
		"%d");

	params.mode = FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP;
	memset(params.mac, 0, sizeof(params.mac));
	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP,
		"%d");

	params.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE;
	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(response.mode, FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
		"%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_mode_match_fail_version_0(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_mode params = { .mode = FP_MODE_MATCH };
	struct ec_response_fp_mode response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_MODE, 0, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_confirm_template_success(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 13> operation = {
		'e', 'n', 'r', 'o', 'l', 'l', '_', 'f', 'i', 'n', 'i', 's', 'h'
	};
	constexpr std::array<const uint8_t, 12> sender = { 'f', 'i', 'n', 'g',
							   'e', 'r', '_', 'g',
							   'u', 'a', 'r', 'd' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_confirm_template params = { 0 };

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = global_context.templ_valid;

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(compute_message_signature(
			session_key, user_id, sender, operation,
			challenge_response.challenge, params.mac),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_CONFIRM_TEMPLATE, 0, &params,
				       sizeof(params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(global_context.templ_valid, 2, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_confirm_template_fail_invalid_signature(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_confirm_template params = { 0 };

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = global_context.templ_valid;

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0, nullptr,
				       0, &challenge_response,
				       sizeof(challenge_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_CONFIRM_TEMPLATE, 0, &params,
				       sizeof(params), nullptr, 0),
		EC_RES_ACCESS_DENIED, "%d");

	TEST_EQ(global_context.templ_valid, 1, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_confirm_template_fail_state_mismatch(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_confirm_template params = { 0 };

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = 0;

	TEST_EQ(test_send_host_command(EC_CMD_FP_CONFIRM_TEMPLATE, 0, &params,
				       sizeof(params), nullptr, 0),
		EC_RES_ERROR, "%d");

	TEST_EQ(global_context.templ_valid, 1, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_sign_match_success(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 4> operation = { 'a', 'u', 't',
							     'h' };
	constexpr std::array<const uint8_t, 5> sender = { 'f', 'p', 'm', 'c',
							  'u' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_sign_match params = {
		.challenge = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
			       8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5 },
	};
	struct ec_response_fp_sign_match response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_signature{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	global_context.positive_match_secret_state.template_matched = 0;
	global_context.positive_match_secret_state.readable = true;
	global_context.positive_match_secret_state.deadline.val =
		get_time().val + 5 * SECOND;

	TEST_EQ(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(compute_message_signature(session_key, user_id, sender,
					  operation, params.challenge,
					  expected_signature),
		EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(response.signature, expected_signature,
			     expected_signature.size());

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_sign_match_fail_no_match(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_sign_match params = { 0 };
	struct ec_response_fp_sign_match response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	global_context.positive_match_secret_state.template_matched =
		FP_NO_SUCH_TEMPLATE;

	TEST_EQ(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_sign_match_fail_deadline_passed(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_sign_match params = { 0 };
	struct ec_response_fp_sign_match response{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 context_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 10, 0, 0, 0, 0, 0, 0, 0 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &context_params,
				       sizeof(context_params), nullptr, 0),
		EC_RES_SUCCESS, "%d");

	global_context.positive_match_secret_state.template_matched = 0;
	global_context.positive_match_secret_state.readable = true;
	global_context.positive_match_secret_state.deadline.val =
		get_time().val - 1;

	TEST_EQ(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_TIMEOUT, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_sign_match_fail_no_session(void)
{
	struct ec_params_fp_sign_match params = { 0 };
	struct ec_response_fp_sign_match response{};

	fp_reset_and_clear_context();
	reset_session();

	TEST_EQ(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_reset_does_not_clear_session(void)
{
	constexpr std::array<const uint8_t, 4> user_id = { 0x0a, 0x00, 0x00,
							   0x00 };
	constexpr std::array<const uint8_t, 4> operation = { 'a', 'u', 't',
							     'h' };
	constexpr std::array<const uint8_t, 5> sender = { 'f', 'p', 'm', 'c',
							  'u' };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_sign_match params = {
		.challenge = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
			       8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5 },
	};
	struct ec_response_fp_sign_match response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_signature{};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(establish_session(session_key), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)global_context.fp_encryption_status,
		      FP_CONTEXT_STATUS_SESSION_ESTABLISHED);

	fp_reset_and_clear_context();

	TEST_BITS_SET((int)global_context.fp_encryption_status,
		      FP_CONTEXT_STATUS_SESSION_ESTABLISHED);

	std::ranges::copy(user_id, global_context.user_id.begin());
	global_context.positive_match_secret_state.template_matched = 0;
	global_context.positive_match_secret_state.readable = true;
	global_context.positive_match_secret_state.deadline.val =
		get_time().val + 5 * SECOND;

	TEST_EQ(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
				       sizeof(params), &response,
				       sizeof(response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(compute_message_signature(session_key, user_id, sender,
					  operation, params.challenge,
					  expected_signature),
		EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(response.signature, expected_signature,
			     expected_signature.size());

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_clears_context(void)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;

	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_1 = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_2 = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	/* 1. Establish first session with tpm_seed_1 */
	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed_1,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       NULL, 0),
		EC_RES_SUCCESS, "%d");

	/* Set UserID and some templates */
	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 1, 2, 3, 4, 5, 6, 7, 8 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &ctx_params,
				       sizeof(ctx_params), NULL, 0),
		EC_RES_SUCCESS, "%d");

	global_context.templ_valid = 1;

	/* Verify current state */
	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)status, FP_ENC_STATUS_SEED_SET);
	TEST_BITS_SET((int)status, FP_CONTEXT_USER_ID_SET);
	TEST_EQ(global_context.templ_valid, 1u, "%u");
	TEST_ASSERT_ARRAY_EQ(global_context.tpm_seed, tpm_seed_1,
			     tpm_seed_1.size());

	/* 2. Establish second session with tpm_seed_2 */
	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed_2,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       NULL, 0),
		EC_RES_SUCCESS, "%d");

	/* 3. Verify that context was cleared and TPM seed updated */
	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)status, FP_ENC_STATUS_SEED_SET);
	/* User ID should be cleared */
	TEST_BITS_CLEARED((int)status, FP_CONTEXT_USER_ID_SET);
	for (uint8_t val : global_context.user_id) {
		TEST_EQ(val, 0, "%d");
	}

	/* Templates should be cleared */
	TEST_EQ(global_context.templ_valid, 0u, "%u");

	/* TPM seed should be updated to tpm_seed_2 */
	TEST_ASSERT_ARRAY_EQ(global_context.tpm_seed, tpm_seed_2,
			     tpm_seed_2.size());

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_session_corrupted_seed(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6,
		7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed,
			&session_params),
		EC_SUCCESS, "%d");

	/* Corrupt the encrypted TPM seed */
	session_params.enc_tpm_seed[0] ^= 0xFF;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	/* Expect failure in TPM Seed decryption. */
	TEST_EQ(rv, EC_RES_ERROR, "%d");

	/* Verify that TPM seed is NOT set and session is NOT established */
	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_CLEARED((int)status, FP_ENC_STATUS_SEED_SET);
	TEST_BITS_CLEARED((int)status, FP_CONTEXT_STATUS_SESSION_ESTABLISHED);

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_reestablish_session_corrupted_seed(void)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_1 = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_2 = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};

	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;

	TEST_EQ(initialize_pairing_key(pairing_key), EC_SUCCESS, "%d");

	/* 1. Establish first session with tpm_seed_1 */
	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed_1,
			&session_params),
		EC_SUCCESS, "%d");

	TEST_EQ(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       &session_params, sizeof(session_params),
				       NULL, 0),
		EC_RES_SUCCESS, "%d");

	/* Verify current state */
	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_SET((int)status, FP_ENC_STATUS_SEED_SET);
	TEST_BITS_SET((int)status, FP_CONTEXT_STATUS_SESSION_ESTABLISHED);

	/* 2. Try to re-establish session with corrupted tpm_seed_2 */
	TEST_EQ(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				       &nonce_response, sizeof(nonce_response)),
		EC_RES_SUCCESS, "%d");

	TEST_EQ(generate_valid_establish_session_request(
			pairing_key, nonce_response.nonce, tpm_seed_2,
			&session_params),
		EC_SUCCESS, "%d");

	/* Corrupt the encrypted TPM seed */
	session_params.enc_tpm_seed[0] ^= 0xFF;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	/* Expect failure in TPM Seed decryption. */
	TEST_EQ(rv, EC_RES_ERROR, "%d");

	/* 3. Verify that the previous session is gone and new one is not set */
	TEST_EQ(get_fp_encryption_status(&status), EC_SUCCESS, "%d");
	TEST_BITS_CLEARED((int)status, FP_ENC_STATUS_SEED_SET);
	TEST_BITS_CLEARED((int)status, FP_CONTEXT_STATUS_SESSION_ESTABLISHED);

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_establish_session_busy(void)
{
	enum ec_status rv;
	struct ec_params_fp_establish_session session_params = {};
	static constexpr uint32_t modes[] = {
		FP_MODE_ENROLL_SESSION,
		FP_MODE_ENROLL_IMAGE,
		FP_MODE_MATCH,
		FP_MODE_RESET_SENSOR,
		FP_MODE_ENCRYPT_TEMPLATE,
		FP_MODE_DECRYPT_TEMPLATE,
	};

	fp_reset_and_clear_context();
	reset_session();

	/* Pretend that the session nonce was generated. */
	global_context.fp_encryption_status |= FP_CONTEXT_SESSION_NONCE_SET;

	for (uint32_t mode : modes) {
		/* Pretend that an operation on templates is running. */
		global_context.sensor_mode = mode;

		rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
					    &session_params,
					    sizeof(session_params), NULL, 0);

		TEST_EQ(rv, EC_RES_BUSY, "mode 0x%x");
	}

	global_context.sensor_mode = 0;

	return EC_SUCCESS;
}

} // namespace

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fp_command_generate_nonce);
	RUN_TEST(test_fp_command_commit_without_seed);

	/*
	 * Set the OTP key here since the following tests require it.
	 */
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		std::ranges::copy(default_fake_otp_key,
				  mock_otp.otp_key_buffer);
	}

	RUN_TEST(test_fp_command_establish_pairing_key_fail);
	RUN_TEST(test_fp_command_establish_pairing_key_keygen);
	RUN_TEST(test_fp_command_establish_and_load_pairing_key);
	RUN_TEST(test_fp_command_load_pairing_key_invalid);
	RUN_TEST(test_fp_command_establish_session);
	RUN_TEST(test_fp_command_establish_session_busy);
	RUN_TEST(test_fp_command_establish_session_clears_context);
	RUN_TEST(test_fp_command_establish_session_corrupted_seed);
	RUN_TEST(test_fp_command_reestablish_session_corrupted_seed);
	RUN_TEST(test_fp_command_establish_session_fail_different_pk);
	RUN_TEST(test_fp_command_establish_session_deny);
	RUN_TEST(
		test_fp_command_establish_session_limit_without_generated_nonce);
	RUN_TEST(test_fp_command_establish_session_limit_normal_context);
	RUN_TEST(test_fp_command_establish_session_limit_twice_1);
	RUN_TEST(test_fp_command_establish_session_limit_twice_2);
	RUN_TEST(test_fp_command_establish_session_load_pk);
	RUN_TEST(test_fp_command_template_decrypted);
	RUN_TEST(test_fp_command_commit_v3);
	RUN_TEST(test_fp_command_commit_trivial_salt);
	RUN_TEST(test_fp_command_generate_challenge);
	RUN_TEST(test_fp_command_generate_challenge_fail_no_session);
	RUN_TEST(test_fp_validate_request);
	RUN_TEST(test_fp_validate_request_fail_no_challenge);
	RUN_TEST(test_fp_validate_request_fail_invalid_signature);
	RUN_TEST(test_fp_validate_request_fail_timeout);
	RUN_TEST(test_fp_sign_message);
	RUN_TEST(test_fp_sign_message_fail_no_session);
	RUN_TEST(test_fp_mode_match_correct_signature);
	RUN_TEST(test_fp_mode_disable_match_no_signature);
	RUN_TEST(test_fp_mode_match_invalid_signature);
	RUN_TEST(test_fp_mode_match_fail_version_0);
	RUN_TEST(test_fp_mode_enroll_correct_signature);
	RUN_TEST(test_fp_mode_disable_enroll_no_signature);
	RUN_TEST(test_fp_mode_enroll_invalid_signature);
	RUN_TEST(test_fp_mode_enroll_session_transitions);
	RUN_TEST(test_fp_confirm_template_success);
	RUN_TEST(test_fp_confirm_template_fail_invalid_signature);
	RUN_TEST(test_fp_confirm_template_fail_state_mismatch);
	RUN_TEST(test_fp_sign_match_success);
	RUN_TEST(test_fp_sign_match_fail_no_match);
	RUN_TEST(test_fp_sign_match_fail_deadline_passed);
	RUN_TEST(test_fp_sign_match_fail_no_session);
	RUN_TEST(test_fp_reset_does_not_clear_session);
	test_print_result();
}
