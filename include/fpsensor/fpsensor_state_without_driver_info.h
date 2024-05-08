/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor states interface without driver info */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_STATE_WITHOUT_DRIVER_INFO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_STATE_WITHOUT_DRIVER_INFO_H

#include "atomic.h"
#include "ec_commands.h"
#include "link_defs.h"
#include "stdbool.h"
#include "stdint.h"
#include "timer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FP_NO_SUCH_TEMPLATE (UINT16_MAX)

/* --- Global variables defined in fpsensor_state_without_driver_info.c --- */

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

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_STATE_WITHOUT_DRIVER_INFO_H */
