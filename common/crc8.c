/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "crc8.h"

uint8_t cros_crc8(const uint8_t *data, int len)
{
	return cros_crc8_arg(data, len, 0);
}

uint8_t cros_crc8_arg(const uint8_t *data, int len, uint8_t previous_crc)
{
	unsigned int crc = previous_crc << 8;
	int i, j;

	for (j = len; j; j--, data++) {
		crc ^= (*data << 8);
		for (i = 8; i; i--) {
			if (crc & 0x8000)
				crc ^= (0x1070 << 3);
			crc <<= 1;
		}
	}

	return (uint8_t)(crc >> 8);
}
