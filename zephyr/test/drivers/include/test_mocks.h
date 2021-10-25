/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fff.h>

/*
 * Convenience macros
 */

/**
 * @brief  Helper macro for inspecting the argument history of a given
 *         fake. Counts number of times the fake was called with a given
 *         argument.
 * @param  FAKE - FFF-provided fake structure (no pointers).
 * @param  ARG_NUM - Zero-based index of the argument to compare.
 * @param  VAL - Expression the argument must equal.
 * @return Returns the number of times a call was made to the fake
 *         where the argument `ARG_NUM` equals `VAL`.
 */
#define MOCK_COUNT_CALLS_WITH_ARG_VALUE(FAKE, ARG_NUM, VAL)              \
	({                                                               \
		int count = 0;                                           \
		for (int i = 0; i < (FAKE).call_count; i++) {            \
			if ((FAKE).arg##ARG_NUM##_history[i] == (VAL)) { \
				count++;                                 \
			}                                                \
		}                                                        \
		count;                                                   \
	})

/*
 * Mock declarations
 */

/* Mocks for common/init_rom.c */
DECLARE_FAKE_VALUE_FUNC(const void *, init_rom_map, const void *, int);
DECLARE_FAKE_VOID_FUNC(init_rom_unmap, const void *, int);
DECLARE_FAKE_VALUE_FUNC(int, init_rom_copy, int, int, int);
