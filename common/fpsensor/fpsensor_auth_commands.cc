/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor.h"
#include "fpsensor_auth_commands.h"
#include "fpsensor_auth_crypto.h"
#include "fpsensor_crypto.h"
#include "fpsensor_state.h"
#include "fpsensor_utils.h"
#include "openssl/mem.h"
#include "scoped_fast_cpu.h"

#include <algorithm>
#include <array>
#include <utility>

enum ec_error_list check_context_cleared()
{
	for (uint32_t partial : user_id)
		if (partial != 0)
			return EC_ERROR_ACCESS_DENIED;
	if (templ_valid != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (templ_dirty != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (positive_match_secret_state.template_matched != FP_NO_SUCH_TEMPLATE)
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
					     FP_AES_KEY_ENC_METADATA_VERSION);
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

	bssl::UniquePtr<EC_KEY> private_key =
		decrypt_private_key(params->encrypted_private_key);
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
				    r->encrypted_pairing_key.data,
				    sizeof(r->encrypted_pairing_key.data));
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP,
		     fp_command_establish_pairing_key_wrap, EC_VER_MASK(0));
