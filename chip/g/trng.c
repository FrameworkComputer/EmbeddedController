/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"

void init_trng(void)
{
	GWRITE(TRNG, POST_PROCESSING_CTRL,
		GC_TRNG_POST_PROCESSING_CTRL_SHUFFLE_BITS_MASK |
		GC_TRNG_POST_PROCESSING_CTRL_CHURN_MODE_MASK);
	GWRITE(TRNG, SLICE_MAX_UPPER_LIMIT, 1);
	GWRITE(TRNG, SLICE_MIN_LOWER_LIMIT, 0);
	GWRITE(TRNG, TIMEOUT_COUNTER, 0x7ff);
	GWRITE(TRNG, TIMEOUT_MAX_TRY_NUM, 4);
	GWRITE(TRNG, POWER_DOWN_B, 1);
	GWRITE(TRNG, GO_EVENT, 1);
}

uint32_t rand(void)
{
	while (GREAD(TRNG, EMPTY)) {
		if (GREAD_FIELD(TRNG, FSM_STATE, FSM_TIMEOUT)) {
			/* TRNG timed out, restart */
			GWRITE(TRNG, STOP_WORK, 1);
			GWRITE(TRNG, GO_EVENT, 1);
		}
	}
	return GREAD(TRNG, READ_DATA);
}

void rand_bytes(void *buffer, size_t len)
{
	int random_togo = 0;
	int buffer_index = 0;
	uint32_t random_value;
	uint8_t *buf = (uint8_t *) buffer;

	/*
	 * Retrieve random numbers in 4 byte quantities and pack as many bytes
	 * as needed into 'buffer'. If len is not divisible by 4, the
	 * remaining random bytes get dropped.
	 */
	while (buffer_index < len) {
		if (!random_togo) {
			random_value = rand();
			random_togo = sizeof(random_value);
		}
		buf[buffer_index++] = random_value >>
			((random_togo-- - 1) * 8);
	}
}
