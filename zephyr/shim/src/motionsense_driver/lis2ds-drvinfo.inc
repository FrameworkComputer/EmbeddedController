/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_lis2ds.h"
#include "driver/stm_mems_common.h"

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
 * Create driver data for each lis2ds drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-lis2ds")
 */
#define CREATE_SENSOR_DATA_LIS2DS(id, drvdata_name) \
	static struct motion_sensor_t drvdata_name;

CREATE_SENSOR_DATA(cros_ec_drvdata_lis2ds, CREATE_SENSOR_DATA_LIS2DS)

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
 * Create a motion_sensor_t entry for each LIS2DS
 * instance(compatible = "cros-ec,lis2ds") in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lis2ds, MOTIONSENSE_CHIP_LIS2DS,
		     MOTIONSENSE_TYPE_ACCEL, lis2ds_drv, LIS2DS_ODR_MIN_VAL,
		     LIS2DS_ODR_MAX_VAL)
