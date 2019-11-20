/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OTI502 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_OTI502_H
#define __CROS_EC_OTI502_H

#define OTI502_I2C_ADDR_FLAGS		0x10

#define OTI502_IDX_AMBIENT	0
#define OTI502_IDX_OBJECT	1

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read. Idx indicates whether to read die
 *			temperature or external temperature.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int oti502_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_OTI502_H */
