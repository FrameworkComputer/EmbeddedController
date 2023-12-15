/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "test_util.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

const uint8_t default_fake_tpm_seed[] = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60, 0xf8, 0x5a, 0xa0,
	0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78,
	0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};
BUILD_ASSERT(sizeof(default_fake_tpm_seed) == FP_CONTEXT_TPM_BYTES);

const uint8_t
	default_fake_fp_positive_match_salt[FP_POSITIVE_MATCH_SALT_BYTES] = {
		0x04, 0x1f, 0x5a, 0xac, 0x5f, 0x79, 0x10, 0xaf,
		0x04, 0x1d, 0x46, 0x3a, 0x5f, 0x08, 0xee, 0xcb
	};
BUILD_ASSERT(sizeof(default_fake_fp_positive_match_salt) ==
	     FP_POSITIVE_MATCH_SALT_BYTES);

const uint8_t trivial_fp_positive_match_salt[FP_POSITIVE_MATCH_SALT_BYTES] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
BUILD_ASSERT(sizeof(trivial_fp_positive_match_salt) ==
	     FP_POSITIVE_MATCH_SALT_BYTES);

int fpsensor_state_mock_set_tpm_seed(
	const uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES])
{
	struct ec_params_fp_seed params;

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	memcpy(params.seed, tpm_seed, FP_CONTEXT_TPM_BYTES);

	return test_send_host_command(EC_CMD_FP_SEED, 0, &params,
				      sizeof(params), NULL, 0);
}
