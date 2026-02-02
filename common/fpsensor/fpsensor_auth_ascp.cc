/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "host_command.h"

#include <ascp/ascp.h>
#include <span>

/* The FPMCU pairing key. */
test_export_static std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

static ec_status fp_command_ascp_establish(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_ascp_establish *>(args->params);
	if (params->pk_g[0] != 0x04) {
		return EC_RES_INVALID_PARAM;
	}
	bssl::UniquePtr<EC_KEY> public_key = create_ec_key_from_pubkey(
		*reinterpret_cast<const fp_elliptic_curve_public_key *>(
			&params->pk_g[1]));
	if (public_key == nullptr) {
		return EC_RES_INVALID_PARAM;
	}

	CleanseWrapper<std::array<uint8_t, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN> >
		sk_f{};
	auto ret = ascp_get_sk_f(sk_f.data(), sk_f.size());
	if (ret != EC_SUCCESS) {
		return EC_RES_ERROR;
	}
	bssl::UniquePtr<EC_KEY> private_key =
		create_ec_key_from_privkey(sk_f.data(), sk_f.size());
	if (private_key == nullptr) {
		return EC_RES_ERROR;
	}

	ret = generate_ecdh_shared_secret_without_kdf(*private_key, *public_key,
						      pairing_key);
	if (ret != EC_SUCCESS) {
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ASCP_ESTABLISH, fp_command_ascp_establish,
		     EC_VER_MASK(0));

enum ec_error_list generate_session_key_with_context(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key)
{
	static constexpr uint8_t session_context[] = { 's', 'd', 'c', 'p' };

	return generate_session_key(fpmcu_nonce, peer_nonce, pairing_key,
				    std::span{ session_context }, session_key);
}
