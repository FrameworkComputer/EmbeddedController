/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for LM4 chip */

#ifndef __CHIP_TEMP_SENSOR_H
#define __CHIP_TEMP_SENSOR_H

struct temp_sensor_t;

/* Temperature reading function. Input pointer to a sensor in temp_sensors.
 * Return temperature in K.
 */
int chip_temp_sensor_read(const struct temp_sensor_t* sensor);

int chip_temp_sensor_init(void);

#endif /* __CHIP_TEMP_SENSOR_H */
