/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Platform.h"
#include "TPM_Types.h"

#include "trng.h"

UINT16 _cpri__GenerateRandom(INT32 randomSize,
			     BYTE *buffer)
{
	int random_togo = 0;
	int buffer_index = 0;
	uint32_t random_value;

	/*
	 * Retrieve random numbers in 4 byte quantities and pack as many bytes
	 * as needed into 'buffer'. If randomSize is not divisible by 4, the
	 * remaining random bytes get dropped.
	 */
	while (buffer_index < randomSize) {
		if (!random_togo) {
			random_value = rand();
			random_togo = sizeof(random_value);
		}
		buffer[buffer_index++] = random_value >>
			((random_togo-- - 1) * 8);
	}

	return randomSize;
}
