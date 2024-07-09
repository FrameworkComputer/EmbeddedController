/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <zephyr/fff.h>

FFF_EXTERN_C

DECLARE_FAKE_VALUE_FUNC(int, mock_alg_init,
			const struct fingerprint_algorithm *const);
DECLARE_FAKE_VALUE_FUNC(int, mock_alg_exit,
			const struct fingerprint_algorithm *const);
DECLARE_FAKE_VALUE_FUNC(int, mock_alg_enroll_start,
			const struct fingerprint_algorithm *const);
DECLARE_FAKE_VALUE_FUNC(int, mock_alg_enroll_step,
			const struct fingerprint_algorithm *const,
			const uint8_t *const, int *);
DECLARE_FAKE_VALUE_FUNC(int, mock_alg_enroll_finish,
			const struct fingerprint_algorithm *const, void *);
DECLARE_FAKE_VALUE_FUNC(int, mock_alg_match,
			const struct fingerprint_algorithm *const, void *,
			uint32_t, const uint8_t *const, int32_t *, uint32_t *);

FFF_END_EXTERN_C
