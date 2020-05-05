/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PI3HDX1204 retimer.
 */

#include "i2c.h"
#include "pi3hdx1204.h"

int pi3hdx1204_enable(const int i2c_port,
		      const uint16_t i2c_addr_flags,
		      const int enable)
{
	uint8_t buf[PI3HDX1204_ENABLE_OFFSET + 1] = {0};

	buf[PI3HDX1204_ENABLE_OFFSET] =
		enable ? PI3HDX1204_ENABLE_ALL_CHANNELS : 0;

	return i2c_xfer(i2c_port, i2c_addr_flags,
			buf, PI3HDX1204_ENABLE_OFFSET + 1,
			NULL, 0);
}
