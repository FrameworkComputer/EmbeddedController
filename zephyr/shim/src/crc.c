/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/sys/crc.h>

#include "crc8.h"

/* Polynomial representation for x^8 + x^2 + x + 1 is 0x07 */
#define SMBUS_POLYNOMIAL 0x07

inline uint8_t cros_crc8(const uint8_t *data, int len)
{
	return crc8(data, len, SMBUS_POLYNOMIAL, 0, false);
}

uint8_t cros_crc8_arg(const uint8_t *data, int len, uint8_t previous_crc)
{
	return crc8(data, len, SMBUS_POLYNOMIAL, previous_crc, false);
}
