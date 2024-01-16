/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_FPSENSOR_STATE_MOCK_H
#define __MOCK_FPSENSOR_STATE_MOCK_H

#include "ec_commands.h"

#include <stdint.h>

extern const uint8_t default_fake_tpm_seed[FP_CONTEXT_TPM_BYTES];
extern const uint8_t
	default_fake_fp_positive_match_salt[FP_POSITIVE_MATCH_SALT_BYTES];
extern const uint8_t
	trivial_fp_positive_match_salt[FP_POSITIVE_MATCH_SALT_BYTES];

int fpsensor_state_mock_set_tpm_seed(
	const uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES]);

#endif /* __MOCK_FPSENSOR_STATE_MOCK_H */
