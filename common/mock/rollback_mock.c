/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Mock rollback block library
 */

#include <stdint.h>
#include <string.h>

#include "common.h"
#include "compile_time_macros.h"
#include "util.h"
#include "mock/rollback_mock.h"

struct mock_ctrl_rollback mock_ctrl_rollback = MOCK_CTRL_DEFAULT_ROLLBACK;

static const uint8_t fake_rollback_secret[] = {
	0xcf, 0xe3, 0x23, 0x76, 0x35, 0x04, 0xc2, 0x0f,
	0x0d, 0xb6, 0x02, 0xa9, 0x68, 0xba, 0x2a, 0x61,
	0x86, 0x2a, 0x85, 0xd1, 0xca, 0x09, 0x54, 0x8a,
	0x6b, 0xe2, 0xe3, 0x38, 0xde, 0x5d, 0x59, 0x14,
};

BUILD_ASSERT(sizeof(fake_rollback_secret) == CONFIG_ROLLBACK_SECRET_SIZE);

/* Mock the rollback for unit or fuzz tests. */
int rollback_get_secret(uint8_t *secret)
{
	if (mock_ctrl_rollback.get_secret_fail)
		return EC_ERROR_UNKNOWN;
	memcpy(secret, fake_rollback_secret, sizeof(fake_rollback_secret));
	return EC_SUCCESS;
}
