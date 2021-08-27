/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_kionix.h"

/*
 * CREATE_SENSOR_DATA which is defined in motionsense_sensors.c is
 * the helper to create sensor driver specific data.
 *
 * CREATE_SENSOR_DATA gets two arguments. One is the compatible
 * property value specified in device tree and the other one is the macro
 * that actually creates sensor driver specific data. The macro gets
 * node id and the name to be used for the sensor driver data.
 */

/*
 * Create driver data for each Kionix drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-kionix")
 */
/* Declare Kionix driver data */
#define CREATE_SENSOR_DATA_KIONIX(id, drvdata_name)	\
	static struct kionix_accel_data drvdata_name;

CREATE_SENSOR_DATA(cros_ec_drvdata_kionix, CREATE_SENSOR_DATA_KIONIX)

/*
 * CREATE_MOTION_SENSOR which is defined in motionsense_sensors.c is
 * the macro to create an entry in motion_sensors array.
 * The macro gets value of compatible property of
 * the sensor in device tree and sensor specific values like chip ID,
 * type of sensor, name of driver, default min/max frequency.
 * Then using the values, it creates the corresponding motion_sense_t entry
 * in motion_sensors array.
 */

/*
 * Create a motion_sensor_t entry for each KX022
 * instance(compatible = "cros-ec,kx022") in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_kx022, MOTIONSENSE_CHIP_KX022,      \
		     MOTIONSENSE_TYPE_ACCEL, kionix_accel_drv,   \
		     KX022_ACCEL_MIN_FREQ, KX022_ACCEL_MAX_FREQ)
