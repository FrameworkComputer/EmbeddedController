/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP006 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TMP006_H
#define __CROS_EC_TMP006_H

/* Registers within the TMP006 chip */
#define TMP006_REG_VOBJ            0x00
#define TMP006_REG_TDIE            0x01
#define TMP006_REG_CONFIG          0x02
#define TMP006_REG_MANUFACTURER_ID 0xfe
#define TMP006_REG_DEVICE_ID       0xff

/* I2C address components */
#define TMP006_ADDR(PORT,REG) ((PORT << 16) + REG)
#define TMP006_PORT(ADDR) (ADDR >> 16)
#define TMP006_REG(ADDR) (ADDR & 0xffff)

struct tmp006_t {
	const char *name;
	int addr;          /* I2C address formed by TMP006_ADDR macro. */
};

/* Names and addresses of the sensors we have */
extern const struct tmp006_t tmp006_sensors[];

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
