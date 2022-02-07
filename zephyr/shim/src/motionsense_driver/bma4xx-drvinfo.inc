/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_bma4xx.h"

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
 * Create driver data for each BMA4xx drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-bma4xx")
 */
/* Declare BMA4xx driver data */
#define CREATE_SENSOR_DATA_BMA4XX(id, drvdata_name)	\
	static struct accelgyro_saved_data_t drvdata_name;

CREATE_SENSOR_DATA(cros_ec_drvdata_bma4xx, CREATE_SENSOR_DATA_BMA4XX)

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
 * Create a motion_sensor_t entry for each BMA4XX
 * instance(compatible = "cros-ec,bma4xx") in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_bma4xx, MOTIONSENSE_CHIP_BMA422,	\
	MOTIONSENSE_TYPE_ACCEL, bma4_accel_drv,		\
	BMA4_ACCEL_MIN_FREQ, BMA4_ACCEL_MAX_FREQ)
