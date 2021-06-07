/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accel_lis2dw12_public.h"

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
 * (compatible = "cros-ec,drvdata-lis2dw12")
 */
/* Declare LIS2DW12 driver data */
#define CREATE_SENSOR_DATA_LIS2DW12(id, drvdata_name)	\
	static struct motion_sensor_t drvdata_name;

CREATE_SENSOR_DATA(cros_ec_drvdata_lis2dw12, CREATE_SENSOR_DATA_LIS2DW12)

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
 * Create a motion_sensor_t entry for each LIS2DW12
 * instance(compatible = "cros-ec,lis2dw12") in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lis2dw12, MOTIONSENSE_CHIP_LIS2DW12,	\
		     MOTIONSENSE_TYPE_ACCEL, lis2dw12_drv,		\
		     LIS2DW12_ODR_MIN_VAL, LIS2DW12_ODR_MAX_VAL)
