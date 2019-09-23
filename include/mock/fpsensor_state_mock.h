/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_FPSENSOR_STATE_MOCK_H
#define __MOCK_FPSENSOR_STATE_MOCK_H

#include <stdbool.h>
#include <stdint.h>
#include "ec_commands.h"

extern const uint8_t default_fake_tpm_seed[FP_CONTEXT_TPM_BYTES];

int fpsensor_state_mock_set_tpm_seed(
	const uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES]);

#endif  /* __MOCK_FPSENSOR_STATE_MOCK_H */
