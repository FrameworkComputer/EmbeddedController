/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G781 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_G781_H
#define __CROS_EC_TEMP_SENSOR_G781_H

#define G781_I2C_ADDR		0x98 /* 7-bit address is 0x4C */

/* Chip-specific commands */
#define G781_TEMP_LOCAL		0x00
#define G781_TEMP_REMOTE	0x01

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read.  Idx indicates whether to read die
 *			temperature or external temperature.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int g781_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_TEMP_SENSOR_G781_H */
