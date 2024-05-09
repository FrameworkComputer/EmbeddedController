/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_STATE_H
#define __CROS_EC_FPSENSOR_FPSENSOR_STATE_H

#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "fpsensor_driver.h"
#include "fpsensor_matcher.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* if no special memory regions are defined, fallback on regular SRAM */
#ifndef FP_FRAME_SECTION
#define FP_FRAME_SECTION
#endif
#ifndef FP_TEMPLATE_SECTION
#define FP_TEMPLATE_SECTION
#endif

#define FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE                         \
	(FP_ALGORITHM_TEMPLATE_SIZE + FP_POSITIVE_MATCH_SALT_BYTES + \
	 sizeof(struct ec_fp_template_encryption_metadata))

/* Events for the FPSENSOR task */
#define TASK_EVENT_SENSOR_IRQ TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_UPDATE_CONFIG TASK_EVENT_CUSTOM_BIT(1)

#define FP_NO_SUCH_TEMPLATE (UINT16_MAX)

/* --- Global variables defined in fpsensor_state.c --- */

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
extern uint8_t fp_positive_match_salt[FP_MAX_FINGER_COUNT]
				     [FP_POSITIVE_MATCH_SALT_BYTES];

struct positive_match_secret_state {
	/* Index of the most recently matched template. */
	uint16_t template_matched;
	/* Flag indicating positive match secret can be read. */
	bool readable;
	/* Deadline to read positive match secret. */
	timestamp_t deadline;
};

struct fpsensor_context {
	/** Index of the last enrolled but not retrieved template. */
	uint16_t template_newly_enrolled;
	/** Number of used templates */
	uint16_t templ_valid;
	/** Bitmap of the templates with local modifications */
	uint32_t templ_dirty;
	/** Status of the FP encryption engine & context. */
	uint32_t fp_encryption_status;
	atomic_t fp_events;
	uint32_t sensor_mode;
	/** Part of the IKM used to derive encryption keys received from the
	 * TPM.
	 */
	uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES];
	/** Current user ID */
	uint8_t user_id[FP_CONTEXT_USERID_BYTES];
	struct positive_match_secret_state positive_match_secret_state;
};

extern struct fpsensor_context global_context;

/*
 * Check if FP TPM seed has been set.
 *
 * @return 1 if the seed has been set, 0 otherwise.
 */
int fp_tpm_seed_is_set(void);

/* Simulation for unit tests. */
__test_only void fp_task_simulate(void);

/*
 * Clear one fingerprint template.
 *
 * @param idx the index of the template to clear.
 */
void fp_clear_finger_context(uint16_t idx);

/**
 * Reset the current user id associated.
 */
void fp_reset_context(void);

/**
 * Init the decrypted template state with the current user_id.
 */
void fp_init_decrypted_template_state_with_user_id(uint16_t idx);

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

/**
 * Change the sensor mode.
 *
 * @param mode          new mode to change to
 * @param mode_output   resulting mode
 * @return EC_RES_SUCCESS on success. Error code on failure.
 */
enum ec_status fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output);

/**
 * Allow reading positive match secret for |fgr| in the next 5 seconds.
 *
 * @param fgr the index of template to enable positive match secret.
 * @param state the state of positive match secret, e.g. readable or not.
 * @return EC_SUCCESS if the request is valid, error code otherwise.
 */
int fp_enable_positive_match_secret(uint16_t fgr,
				    struct positive_match_secret_state *state);

/**
 * Disallow positive match secret for any finger to be read.
 *
 * @param state the state of positive match secret, e.g. readable or not.
 */
void fp_disable_positive_match_secret(struct positive_match_secret_state *state);

/**
 * Read the match secret from the positive match salt.
 *
 * @param fgr the index of positive match salt.
 * @param positive_match_secret the match secret that derived from the salt.
 */
enum ec_status fp_read_match_secret(
	int8_t fgr,
	uint8_t positive_match_secret[FP_POSITIVE_MATCH_SECRET_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_STATE_H */
