/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cryptoc/util.h"
#include "ec_commands.h"
#include "fpsensor.h"
#include "fpsensor_crypto.h"
#include "fpsensor_private.h"
#include "fpsensor_state.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "util.h"

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE] FP_FRAME_SECTION __aligned(4);
/* Fingers templates for the current user */
uint8_t fp_template[FP_MAX_FINGER_COUNT][FP_ALGORITHM_TEMPLATE_SIZE]
	FP_TEMPLATE_SECTION;
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
uint8_t fp_enc_buffer[FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE]
	FP_TEMPLATE_SECTION;
/* Salt used in derivation of positive match secret. */
uint8_t fp_positive_match_salt
	[FP_MAX_FINGER_COUNT][FP_POSITIVE_MATCH_SALT_BYTES];

struct positive_match_secret_state positive_match_secret_state = {
	.template_matched = FP_NO_SUCH_TEMPLATE,
	.readable = false,
	.deadline.val = 0,
};

/* Index of the last enrolled but not retrieved template. */
int8_t template_newly_enrolled = FP_NO_SUCH_TEMPLATE;
/* Number of used templates */
uint32_t templ_valid;
/* Bitmap of the templates with local modifications */
uint32_t templ_dirty;
/* Current user ID */
uint32_t user_id[FP_CONTEXT_USERID_WORDS];
/* Part of the IKM used to derive encryption keys received from the TPM. */
uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES];
/* Status of the FP encryption engine. */
static uint32_t fp_encryption_status;

uint32_t fp_events;

uint32_t sensor_mode;

void fp_task_simulate(void)
{
	int timeout_us = -1;

	while (1)
		task_wait_event(timeout_us);
}

void fp_clear_finger_context(int idx)
{
	always_memset(fp_template[idx], 0, sizeof(fp_template[0]));
	always_memset(fp_positive_match_salt[idx], 0,
		      sizeof(fp_positive_match_salt[0]));
}

/**
 * @warning |fp_buffer| contains data used by the matching algorithm that must
 * be released by calling fp_sensor_deinit() first. Call
 * fp_reset_and_clear_context instead of calling this directly.
 */
static void _fp_clear_context(void)
{
	int idx;

	templ_valid = 0;
	templ_dirty = 0;
	always_memset(fp_buffer, 0, sizeof(fp_buffer));
	always_memset(fp_enc_buffer, 0, sizeof(fp_enc_buffer));
	always_memset(user_id, 0, sizeof(user_id));
	fp_disable_positive_match_secret(&positive_match_secret_state);
	for (idx = 0; idx < FP_MAX_FINGER_COUNT; idx++)
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
	uint32_t event_out = deprecated_atomic_read_clear(&fp_events);

	memcpy(out, &event_out, sizeof(event_out));

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_FINGERPRINT, fp_get_next_event);

static enum ec_status fp_command_tpm_seed(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_seed *params = args->params;

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

int fp_tpm_seed_is_set(void)
{
	return fp_encryption_status & FP_ENC_STATUS_SEED_SET;
}

static enum ec_status
fp_command_encryption_status(struct host_cmd_handler_args *args)
{
	struct ec_response_fp_encryption_status *r = args->response;

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

int fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output)
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
		task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG, 0);
	}

	*mode_output = sensor_mode;
	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_mode *p = args->params;
	struct ec_response_fp_mode *r = args->response;

	int ret = fp_set_sensor_mode(p->mode, &r->mode);

	if (ret == EC_RES_SUCCESS)
		args->response_size = sizeof(*r);

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MODE, fp_command_mode, EC_VER_MASK(0));

static enum ec_status fp_command_context(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_context_v1 *p = args->params;
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

		memcpy(user_id, p->userid, sizeof(user_id));
		return EC_RES_SUCCESS;
	}

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONTEXT, fp_command_context, EC_VER_MASK(1));

int fp_enable_positive_match_secret(uint32_t fgr,
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

void fp_disable_positive_match_secret(
	struct positive_match_secret_state *state)
{
	state->template_matched = FP_NO_SUCH_TEMPLATE;
	state->readable = false;
	state->deadline.val = 0;
}

static enum ec_status fp_command_read_match_secret(
	struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_read_match_secret *params = args->params;
	struct ec_response_fp_read_match_secret *response = args->response;
	int8_t fgr = params->fgr;
	timestamp_t now = get_time();
	struct positive_match_secret_state state_copy
		= positive_match_secret_state;

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
			"be read now.", fgr);
		return EC_RES_ACCESS_DENIED;
	}

	if (derive_positive_match_secret(response->positive_match_secret,
					 fp_positive_match_salt[fgr])
		!= EC_SUCCESS) {
		CPRINTS("Failed to derive positive match secret for finger %d",
			fgr);
		/* Keep the template and encryption salt. */
		return EC_RES_ERROR;
	}
	CPRINTS("Derived positive match secret for finger %d", fgr);
	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_READ_MATCH_SECRET, fp_command_read_match_secret,
		     EC_VER_MASK(0));
