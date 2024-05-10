/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_state_without_driver_info.h"

/* TODO(b/286119221): Refactor fingerprint sensor state. */

extern "C" {

struct fpsensor_context global_context = {
	.template_newly_enrolled = FP_NO_SUCH_TEMPLATE,
	.templ_valid = 0,
	.templ_dirty = 0,
	.fp_events = 0,
	.sensor_mode = 0,
	.tpm_seed = { 0 },
	.user_id = { 0 },
	.positive_match_secret_state = {
		.template_matched = FP_NO_SUCH_TEMPLATE,
		.readable = false,
		.deadline = {
			.val = 0,
		}},
};

int fp_tpm_seed_is_set(void)
{
	return global_context.fp_encryption_status & FP_ENC_STATUS_SEED_SET;
}
}
