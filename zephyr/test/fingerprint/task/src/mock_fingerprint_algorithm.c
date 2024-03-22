/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_fingerprint_algorithm.h"

#include <zephyr/fff.h>

#include <fingerprint/fingerprint_alg.h>

DEFINE_FAKE_VALUE_FUNC(int, mock_alg_init,
		       const struct fingerprint_algorithm *const);
DEFINE_FAKE_VALUE_FUNC(int, mock_alg_exit,
		       const struct fingerprint_algorithm *const);
DEFINE_FAKE_VALUE_FUNC(int, mock_alg_enroll_start,
		       const struct fingerprint_algorithm *const);
DEFINE_FAKE_VALUE_FUNC(int, mock_alg_enroll_step,
		       const struct fingerprint_algorithm *const,
		       const uint8_t *const, int *);
DEFINE_FAKE_VALUE_FUNC(int, mock_alg_enroll_finish,
		       const struct fingerprint_algorithm *const, void *);
DEFINE_FAKE_VALUE_FUNC(int, mock_alg_match,
		       const struct fingerprint_algorithm *const, void *,
		       uint32_t, const uint8_t *const, int32_t *, uint32_t *);

const struct fingerprint_algorithm_api mock_alg_api = {
	.init = mock_alg_init,
	.exit = mock_alg_exit,
	.enroll_start = mock_alg_enroll_start,
	.enroll_step = mock_alg_enroll_step,
	.enroll_finish = mock_alg_enroll_finish,
	.match = mock_alg_match,
};

FINGERPRINT_ALGORITHM_DEFINE(mock_algorithm, NULL, &mock_alg_api);
