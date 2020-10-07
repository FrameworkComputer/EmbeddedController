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
	const uint8_t buf[PI3HDX1204_DE_OFFSET + 1] = {
		[PI3HDX1204_ACTIVITY_OFFSET] = 0, /* Read Only */
		[PI3HDX1204_NOT_USED_OFFSET] = 0, /* Read Only */
		[PI3HDX1204_ENABLE_OFFSET] =
			enable ? PI3HDX1204_ENABLE_ALL_CHANNELS : 0,
		[PI3HDX1204_EQ_CH0_CH1_OFFSET] =
			pi3hdx1204_tuning.eq_ch0_ch1_offset,
		[PI3HDX1204_EQ_CH2_CH3_OFFSET] =
			pi3hdx1204_tuning.eq_ch2_ch3_offset,
		[PI3HDX1204_VOD_OFFSET] = pi3hdx1204_tuning.vod_offset,
		[PI3HDX1204_DE_OFFSET] = pi3hdx1204_tuning.de_offset,
	};
	int rv;

	rv = i2c_xfer(i2c_port, i2c_addr_flags,
		      buf, PI3HDX1204_DE_OFFSET + 1,
		      NULL, 0);

	if (rv)
		ccprints("pi3hdx1204 enable failed: %d", rv);

	return rv;
}
