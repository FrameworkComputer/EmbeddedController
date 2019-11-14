/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock rollback block library
 */

#ifndef __MOCK_ROLLBACK_MOCK_H
#define __MOCK_ROLLBACK_MOCK_H

#include <stdbool.h>

struct mock_ctrl_rollback {
	bool get_secret_fail;
};

#define MOCK_CTRL_DEFAULT_ROLLBACK             \
(struct mock_ctrl_rollback) {                  \
	.get_secret_fail = false,              \
}

extern struct mock_ctrl_rollback mock_ctrl_rollback;

#endif  /* __MOCK_ROLLBACK_MOCK_H */
