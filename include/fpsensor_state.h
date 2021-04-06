/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_STATE_H
#define __CROS_EC_FPSENSOR_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "common.h"
#include "ec_commands.h"
#include "link_defs.h"
#include "timer.h"

#include "driver/fingerprint/fpsensor.h"

/* if no special memory regions are defined, fallback on regular SRAM */
#ifndef FP_FRAME_SECTION
#define FP_FRAME_SECTION
#endif
#ifndef FP_TEMPLATE_SECTION
#define FP_TEMPLATE_SECTION
#endif

#define SBP_ENC_KEY_LEN 16
#define FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE \
	(FP_ALGORITHM_TEMPLATE_SIZE + \
		FP_POSITIVE_MATCH_SALT_BYTES + \
		sizeof(struct ec_fp_template_encryption_metadata))

/* Events for the FPSENSOR task */
#define TASK_EVENT_SENSOR_IRQ     TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_UPDATE_CONFIG  TASK_EVENT_CUSTOM_BIT(1)

#define FP_NO_SUCH_TEMPLATE -1

/* --- Global variables defined in fpsensor_state.c --- */

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
extern uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE];
/* Fingers templates for the current user */
extern uint8_t fp_template[FP_MAX_FINGER_COUNT][FP_ALGORITHM_TEMPLATE_SIZE];
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
extern uint8_t fp_enc_buffer[FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE];
/* Salt used in derivation of positive match secret. */
extern uint8_t fp_positive_match_salt
	[FP_MAX_FINGER_COUNT][FP_POSITIVE_MATCH_SALT_BYTES];
/* Index of the last enrolled but not retrieved template. */
extern int8_t template_newly_enrolled;
/* Number of used templates */
extern uint32_t templ_valid;
/* Bitmap of the templates with local modifications */
extern uint32_t templ_dirty;
/* Current user ID */
extern uint32_t user_id[FP_CONTEXT_USERID_WORDS];
/* Part of the IKM used to derive encryption keys received from the TPM. */
extern uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES];

extern uint32_t fp_events;

extern uint32_t sensor_mode;

struct positive_match_secret_state {
	/* Index of the most recently matched template. */
	int8_t template_matched;
	/* Flag indicating positive match secret can be read. */
	bool readable;
	/* Deadline to read positive match secret. */
	timestamp_t deadline;
};

extern struct positive_match_secret_state positive_match_secret_state;

/* Simulation for unit tests. */
void fp_task_simulate(void);

/*
 * Clear one fingerprint template.
 *
 * @param idx the index of the template to clear.
 */
void fp_clear_finger_context(int idx);

/**
 * Clear all fingerprint templates associated with the current user id and
 * reset the sensor.
 */
void fp_reset_and_clear_context(void);

/*
 * Get the next FP event.
 *
 * @param out the pointer to the output event.
 */
int fp_get_next_event(uint8_t *out);

/*
 * Check if FP TPM seed has been set.
 *
 * @return 1 if the seed has been set, 0 otherwise.
 */
int fp_tpm_seed_is_set(void);

/**
 * Change the sensor mode.
 *
 * @param mode          new mode to change to
 * @param mode_output   resulting mode
 * @return EC_RES_SUCCESS on success. Error code on failure.
 */
int fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output);

/**
 * Allow reading positive match secret for |fgr| in the next 5 seconds.
 *
 * @param fgr the index of template to enable positive match secret.
 * @param state the state of positive match secret, e.g. readable or not.
 * @return EC_SUCCESS if the request is valid, error code otherwise.
 */
int fp_enable_positive_match_secret(uint32_t fgr,
	struct positive_match_secret_state *state);

/**
 * Disallow positive match secret for any finger to be read.
 *
 * @param state the state of positive match secret, e.g. readable or not.
 */
void fp_disable_positive_match_secret(
	struct positive_match_secret_state *state);

#endif /* __CROS_EC_FPSENSOR_STATE_H */
