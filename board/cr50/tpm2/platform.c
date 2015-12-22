/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Platform.h"
#include "TPM_Types.h"

#include "trng.h"

uint16_t _cpri__GenerateRandom(size_t random_size,
			uint8_t *buffer)
{
	rand_bytes(buffer, random_size);
	return random_size;
}
