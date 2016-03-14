/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"

void init_trng(void)
{
	GWRITE(TRNG, POWER_DOWN_B, 1);
	GWRITE(TRNG, GO_EVENT, 1);
	while (GREAD(TRNG, EMPTY))
		;
	GREAD(TRNG, READ_DATA);
}

uint32_t rand(void)
{
	while (GREAD(TRNG, EMPTY)) {
		if (!GREAD_FIELD(TRNG, FSM_STATE, FSM_IDLE))
			continue;

		/* TRNG must have stopped, needs to be restarted. */
		GWRITE(TRNG, STOP_WORK, 1);
		init_trng();
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
