/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TMP112_H
#define __CROS_EC_TMP112_H

#include "i2c.h"

#define TMP112_I2C_ADDR_FLAGS (0x48 | I2C_FLAG_BIG_ENDIAN)

#define TMP112_REG_TEMP	0x00
#define TMP112_REG_CONF	0x01
#define TMP112_REG_HYST	0x02
#define TMP112_REG_MAX	0x03

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read. (Ignored)
 *
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp112_get_val(int idx, int *temp_ptr);

#endif /* __CROS_EC_TMP112_H */
