/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock rollback block library
 */

#ifndef __MOCK_ROLLBACK_LATEST_MOCK_H
#define __MOCK_ROLLBACK_LATEST_MOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mock_ctrl_latest_rollback_type {
	GET_LATEST_ROLLBACK_FAIL,
	GET_LATEST_ROLLBACK_ZEROS,
	GET_LATEST_ROLLBACK_REAL,
};

struct mock_ctrl_latest_rollback {
	enum mock_ctrl_latest_rollback_type output_type;
};

#define MOCK_CTRL_DEFAULT_LATEST_ROLLBACK    \
	((struct mock_ctrl_latest_rollback){ \
		.output_type = GET_LATEST_ROLLBACK_REAL })

extern struct mock_ctrl_latest_rollback mock_ctrl_latest_rollback;

extern const struct rollback_data fake_latest_rollback_zeros;

extern const struct rollback_data fake_latest_rollback_real;

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_ROLLBACK_LATEST_MOCK_H */
