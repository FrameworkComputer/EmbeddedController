/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for LM4 chip */

#ifndef __CHIP_TEMP_SENSOR_H
#define __CHIP_TEMP_SENSOR_H

struct temp_sensor_t;

/* Temperature polling function. */
int chip_temp_sensor_poll(void);

/* Temperature reading function. Return temperature in K. */
int chip_temp_sensor_get_val(int idx);

int chip_temp_sensor_init(void);

#endif /* __CHIP_TEMP_SENSOR_H */
