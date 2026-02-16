/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H
#define __CROS_EC_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H

#include "common.h"
#include "openssl/sha.h"

#include <array>
#include <span>

/**
 * Check the context has been cleared or not.
 *
 * @return EC_SUCCESS if the context has been cleared
 * @return EC_ERROR_ACCESS_DENIED on the other cases.
 */
enum ec_error_list check_context_cleared();

/**
 * Check if Fingerprint Auth is enabled
 *
 * @return true if the Fingerprint Auth is enabled, false otherwise.
 */
bool fingerprint_auth_enabled();

/**
 * Validate request from Trusted Application.
 *
 * @param[in] context the context.
 * @param[in] operation the requested operation.
 * @param[in] mac the mac that authenticates the request.
 * @return EC_SUCCESS if the request was validated successfully.
 * @return EC_ERROR_ACCESS_DENIED if failed to validate the request.
 * @return EC_ERROR_TIMEOUT if the challenge is too old.
 * @return EC_ERROR_INVAL if failed to compute expected mac.
 */
enum ec_error_list
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac);

/**
 * Sign message to the Trusted Application.
 *
 * @param[in] context the context.
 * @param[in] operation the operation we authenticate.
 * @param[in] peer_challenge the peer's challenge.
 * @param[out] output the mac that authenticates the message.
 * @return EC_SUCCESS if the request was validated successfully.
 * @return EC_ERROR_INVAL if failed to compute the mac.
 */
enum ec_error_list
sign_message(std::span<const uint8_t> context,
	     std::span<const uint8_t> operation,
	     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge,
	     std::span<uint8_t, SHA256_DIGEST_LENGTH> output);

/**
 * Call generate_session_key with current pairing key and context.
 *
 * @param[in] fpmcu_nonce the session nonce on FPMCU side
 * @param[in] peer_nonce the session nonce on peer side
 * @param[in,out] session_key the output key
 *
 * @return EC_SUCCESS on success
 * @return EC_ERROR_* on error
 */
enum ec_error_list generate_session_key_with_context(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key);

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H */
