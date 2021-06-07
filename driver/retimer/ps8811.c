/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8811 retimer.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "ps8811.h"
#include "usb_mux.h"

int ps8811_i2c_read(const struct usb_mux *me, int page, int offset, int *data)
{
	int rv;

	rv = i2c_read8(me->i2c_port,
		       me->i2c_addr_flags + page,
		       offset, data);

	return rv;
}

int ps8811_i2c_write(const struct usb_mux *me, int page, int offset, int data)
{
	int rv;

	rv = i2c_write8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, data);

	return rv;
}

int ps8811_i2c_field_update(const struct usb_mux *me, int page, int offset,
			     uint8_t field_mask, uint8_t set_value)
{
	int rv;

	rv = i2c_field_update8(me->i2c_port,
			       me->i2c_addr_flags + page,
			       offset,
			       field_mask,
			       set_value);

	return rv;
}
