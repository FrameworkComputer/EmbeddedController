/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_lis2dh.h"

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
 * Create driver data for each lis2de drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-lis2de")
 */
#define CREATE_SENSOR_DATA_LIS2DE(id, drvdata_name) \
	static struct accelgyro_saved_data_t drvdata_name;

CREATE_SENSOR_DATA(cros_ec_drvdata_lis2de, CREATE_SENSOR_DATA_LIS2DE)

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
 * Create a motion_sensor_t entry for each LIS2DE
 * instance(compatible = "cros-ec,lis2de") in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lis2de, MOTIONSENSE_CHIP_LIS2DE,
		     MOTIONSENSE_TYPE_ACCEL, lis2dh_drv, LIS2DH_ODR_MIN_VAL,
		     LIS2DH_ODR_MAX_VAL)
