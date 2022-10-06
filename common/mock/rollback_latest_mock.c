/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Mock rollback block library
 */

#include <string.h>

#include "common.h"
#include "compile_time_macros.h"
#include "util.h"
#include "mock/rollback_latest_mock.h"
#include "rollback_private.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_ctrl_latest_rollback mock_ctrl_latest_rollback =
	MOCK_CTRL_DEFAULT_LATEST_ROLLBACK;

const struct rollback_data fake_latest_rollback_zeros = {
	.cookie = 0,
	.secret = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},

	.id = 0,
	.rollback_min_version = 0,
};

const struct rollback_data fake_latest_rollback_real = {
	.cookie = 9,
	.secret = {
	0xcf, 0xe3, 0x23, 0x76, 0x35, 0x04, 0xc2, 0x0f, 0x0d, 0xb6, 0x02,
	0xa9, 0x68, 0xba, 0x2a, 0x61, 0x86, 0x2a, 0x85, 0xd1, 0xca, 0x09,
	0x54, 0x8a, 0x6b, 0xe2, 0xe3, 0x38, 0xde, 0x5d, 0x59, 0x14,},
	.id = 2,
	.rollback_min_version = 1,
};

/* Mock the rollback for unit or fuzz tests. */
int get_latest_rollback(struct rollback_data *data)
{
	switch (mock_ctrl_latest_rollback.output_type) {
	case GET_LATEST_ROLLBACK_FAIL:
		return -5;
	case GET_LATEST_ROLLBACK_ZEROS:
		*data = fake_latest_rollback_zeros;
		break;
	case GET_LATEST_ROLLBACK_REAL:
		*data = fake_latest_rollback_real;
		break;
	}
	return EC_SUCCESS;
}
