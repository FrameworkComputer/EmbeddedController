/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crc.h"
#include "crc8.h"

#include <zephyr/sys/crc.h>

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

uint16_t cros_crc16(const uint8_t *data, int len, uint16_t previous_crc)
{
	return crc16_itu_t(previous_crc, data, len);
}
