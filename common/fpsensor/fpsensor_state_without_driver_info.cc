/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_state_without_driver_info.h"

/* TODO(b/286119221): Refactor fingerprint sensor state. */

extern "C" {

struct positive_match_secret_state
	positive_match_secret_state = { .template_matched = FP_NO_SUCH_TEMPLATE,
					.readable = false,
					.deadline = {
						.val = 0,
					} };

/* Number of used templates */
test_mockable uint16_t templ_valid;
/* Bitmap of the templates with local modifications */
uint32_t templ_dirty;
/* Status of the FP encryption engine & context. */
uint32_t fp_encryption_status;

atomic_t fp_events;

uint32_t sensor_mode;

struct fpsensor_context global_context = { .template_newly_enrolled =
						   FP_NO_SUCH_TEMPLATE,
					   .tpm_seed = { 0 },
					   .user_id = { 0 } };

int fp_tpm_seed_is_set(void)
{
	return fp_encryption_status & FP_ENC_STATUS_SEED_SET;
}
}
