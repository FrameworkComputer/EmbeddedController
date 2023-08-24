/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"

#include <array>

/* Boringssl headers need to be included before extern "C" section. */
#include "openssl/mem.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/shell/shell.h>
#endif

extern "C" {
#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "util.h"
}

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_utils.h"

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE] FP_FRAME_SECTION __aligned(4);
/* Fingers templates for the current user */
test_mockable uint8_t
	fp_template[FP_MAX_FINGER_COUNT]
		   [FP_ALGORITHM_TEMPLATE_SIZE] FP_TEMPLATE_SECTION;
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
uint8_t fp_enc_buffer[FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE] FP_TEMPLATE_SECTION;
/* Salt used in derivation of positive match secret. */
uint8_t fp_positive_match_salt[FP_MAX_FINGER_COUNT]
			      [FP_POSITIVE_MATCH_SALT_BYTES];

/* LCOV_EXCL_START */
__test_only void fp_task_simulate(void)
{
	int timeout_us = -1;

	while (1)
		task_wait_event(timeout_us);
}
/* LCOV_EXCL_STOP */

void fp_clear_finger_context(uint16_t idx)
{
	OPENSSL_cleanse(fp_template[idx], sizeof(fp_template[0]));
	OPENSSL_cleanse(fp_positive_match_salt[idx],
			sizeof(fp_positive_match_salt[0]));
}

void fp_reset_context()
{
	templ_valid = 0;
	templ_dirty = 0;
	template_newly_enrolled = FP_NO_SUCH_TEMPLATE;
	fp_encryption_status &= FP_ENC_STATUS_SEED_SET;
	OPENSSL_cleanse(fp_enc_buffer, sizeof(fp_enc_buffer));
	OPENSSL_cleanse(user_id, sizeof(user_id));
	OPENSSL_cleanse(auth_nonce.data(), auth_nonce.size());
	fp_disable_positive_match_secret(&positive_match_secret_state);
}

/**
 * @warning |fp_buffer| contains data used by the matching algorithm that must
 * be released by calling fp_sensor_deinit() first. Call
 * fp_reset_and_clear_context instead of calling this directly.
 */
static void _fp_clear_context(void)
{
	fp_reset_context();
	OPENSSL_cleanse(fp_buffer, sizeof(fp_buffer));
	for (uint16_t idx = 0; idx < FP_MAX_FINGER_COUNT; idx++)
		fp_clear_finger_context(idx);
}

void fp_reset_and_clear_context(void)
{
	if (fp_sensor_deinit() != EC_SUCCESS)
		CPRINTS("Failed to deinit sensor");
	_fp_clear_context();
	if (fp_sensor_init() != EC_SUCCESS)
		CPRINTS("Failed to init sensor");
}

int fp_get_next_event(uint8_t *out)
{
	uint32_t event_out = atomic_clear(&fp_events);

	memcpy(out, &event_out, sizeof(event_out));

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_FINGERPRINT, fp_get_next_event);

static enum ec_status fp_command_tpm_seed(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_seed *>(args->params);

	if (params->struct_version != FP_TEMPLATE_FORMAT_VERSION) {
		CPRINTS("Invalid seed format %d", params->struct_version);
		return EC_RES_INVALID_PARAM;
	}

	if (fp_encryption_status & FP_ENC_STATUS_SEED_SET) {
		CPRINTS("Seed has already been set.");
		return EC_RES_ACCESS_DENIED;
	}
	memcpy(tpm_seed, params->seed, sizeof(tpm_seed));
	fp_encryption_status |= FP_ENC_STATUS_SEED_SET;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SEED, fp_command_tpm_seed, EC_VER_MASK(0));

static enum ec_status
fp_command_encryption_status(struct host_cmd_handler_args *args)
{
	auto *r =
		static_cast<ec_response_fp_encryption_status *>(args->response);

	r->valid_flags = FP_ENC_STATUS_SEED_SET;
	r->status = fp_encryption_status;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ENC_STATUS, fp_command_encryption_status,
		     EC_VER_MASK(0));

static int validate_fp_mode(const uint32_t mode)
{
	uint32_t capture_type = FP_CAPTURE_TYPE(mode);
	uint32_t algo_mode = mode & ~FP_MODE_CAPTURE_TYPE_MASK;
	uint32_t cur_mode = sensor_mode;

	if (capture_type >= FP_CAPTURE_TYPE_MAX)
		return EC_ERROR_INVAL;

	if (algo_mode & ~FP_VALID_MODES)
		return EC_ERROR_INVAL;

	if ((mode & FP_MODE_ENROLL_SESSION) &&
	    templ_valid >= FP_MAX_FINGER_COUNT) {
		CPRINTS("Maximum number of fingers already enrolled: %d",
			FP_MAX_FINGER_COUNT);
		return EC_ERROR_INVAL;
	}

	/* Don't allow sensor reset if any other mode is
	 * set (including FP_MODE_RESET_SENSOR itself).
	 */
	if (mode & FP_MODE_RESET_SENSOR) {
		if (cur_mode & FP_VALID_MODES)
			return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

enum ec_status fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output)
{
	int ret;

	if (mode_output == NULL)
		return EC_RES_INVALID_PARAM;

	ret = validate_fp_mode(mode);
	if (ret != EC_SUCCESS) {
		CPRINTS("Invalid FP mode 0x%x", mode);
		return EC_RES_INVALID_PARAM;
	}

	if (!(mode & FP_MODE_DONT_CHANGE)) {
		sensor_mode = mode;
		task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
	}

	*mode_output = sensor_mode;
	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_mode(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_mode *>(args->params);
	auto *r = static_cast<ec_response_fp_mode *>(args->response);

	enum ec_status ret = fp_set_sensor_mode(p->mode, &r->mode);

	if (ret == EC_RES_SUCCESS)
		args->response_size = sizeof(*r);

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MODE, fp_command_mode, EC_VER_MASK(0));

static enum ec_status fp_command_context(struct host_cmd_handler_args *args)
{
	const auto *p =
		static_cast<const ec_params_fp_context_v1 *>(args->params);
	uint32_t mode_output;

	switch (p->action) {
	case FP_CONTEXT_ASYNC:
		if (sensor_mode & FP_MODE_RESET_SENSOR)
			return EC_RES_BUSY;

		/**
		 * Trigger a call to fp_reset_and_clear_context() by
		 * requesting a reset. Since that function triggers a call to
		 * fp_sensor_open(), this must be asynchronous because
		 * fp_sensor_open() can take ~175 ms. See http://b/137288498.
		 */
		return fp_set_sensor_mode(FP_MODE_RESET_SENSOR, &mode_output);

	case FP_CONTEXT_GET_RESULT:
		if (sensor_mode & FP_MODE_RESET_SENSOR)
			return EC_RES_BUSY;

		if (fp_encryption_status &
		    FP_CONTEXT_STATUS_NONCE_CONTEXT_SET) {
			/* Reject the request to prevent downgrade attack. */
			return EC_RES_ACCESS_DENIED;
		}

		memcpy(user_id, p->userid, sizeof(user_id));

		/* Set the FP_CONTEXT_USER_ID_SET bit if the user_id is
		 * non-zero. */
		for (size_t i = 0; i < std::size(user_id); i++) {
			if (user_id[i] != 0) {
				fp_encryption_status |= FP_CONTEXT_USER_ID_SET;
				break;
			}
		}

		return EC_RES_SUCCESS;
	}

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONTEXT, fp_command_context, EC_VER_MASK(1));

int fp_enable_positive_match_secret(uint16_t fgr,
				    struct positive_match_secret_state *state)
{
	timestamp_t now;

	if (state->readable) {
		CPRINTS("Error: positive match secret already readable.");
		fp_disable_positive_match_secret(state);
		return EC_ERROR_UNKNOWN;
	}

	now = get_time();
	state->template_matched = fgr;
	state->readable = true;
	state->deadline.val = now.val + (5 * SECOND);
	return EC_SUCCESS;
}

void fp_disable_positive_match_secret(struct positive_match_secret_state *state)
{
	state->template_matched = FP_NO_SUCH_TEMPLATE;
	state->readable = false;
	state->deadline.val = 0;
}

enum ec_status fp_read_match_secret(
	int8_t fgr,
	uint8_t positive_match_secret[FP_POSITIVE_MATCH_SECRET_BYTES])
{
	timestamp_t now = get_time();
	struct positive_match_secret_state state_copy =
		positive_match_secret_state;

	fp_disable_positive_match_secret(&positive_match_secret_state);

	if (fgr < 0 || fgr >= FP_MAX_FINGER_COUNT) {
		CPRINTS("Invalid finger number %d", fgr);
		return EC_RES_INVALID_PARAM;
	}
	if (timestamp_expired(state_copy.deadline, &now)) {
		CPRINTS("Reading positive match secret disallowed: "
			"deadline has passed.");
		return EC_RES_TIMEOUT;
	}
	if (fgr != state_copy.template_matched || !state_copy.readable) {
		CPRINTS("Positive match secret for finger %d is not meant to "
			"be read now.",
			fgr);
		return EC_RES_ACCESS_DENIED;
	}

	if (derive_positive_match_secret(positive_match_secret,
					 fp_positive_match_salt[fgr]) !=
	    EC_SUCCESS) {
		CPRINTS("Failed to derive positive match secret for finger %d",
			fgr);
		/* Keep the template and encryption salt. */
		return EC_RES_ERROR;
	}
	CPRINTS("Derived positive match secret for finger %d", fgr);

	return EC_RES_SUCCESS;
}

static enum ec_status
fp_command_read_match_secret(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_read_match_secret *>(
			args->params);
	auto *response =
		static_cast<ec_response_fp_read_match_secret *>(args->response);
	int8_t fgr = params->fgr;

	ec_status ret =
		fp_read_match_secret(fgr, response->positive_match_secret);

	if (ret != EC_RES_SUCCESS) {
		return ret;
	}

	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_READ_MATCH_SECRET, fp_command_read_match_secret,
		     EC_VER_MASK(0));
