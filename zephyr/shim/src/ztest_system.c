/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

/* Ongoing actions preventing going into deep-sleep mode. */
uint32_t sleep_mask;

int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	return EC_SUCCESS;
}

const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size)
{
	return NULL;
}
