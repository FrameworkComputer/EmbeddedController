/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP006 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TMP006_H
#define __CROS_EC_TMP006_H

#define TMP006_ADDR(PORT,REG) ((PORT << 16) + REG)
#define TMP006_PORT(ADDR) (ADDR >> 16)
#define TMP006_REG(ADDR) (ADDR & 0xffff)

struct tmp006_t {
	const char* name;
	int addr;          /* I2C address formed by TMP006_ADDR macro. */
};

/**
 * Poll all TMP006 sensors.
 *
 * @return 0 if successful, non-zero if error. */
int tmp006_poll(void);

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read.  The low bit in idx indicates whether
 *			to read die temperature or object temperature.  The
 *			other bits serve as internal index to tmp006 module.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp006_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_TMP006_H */
