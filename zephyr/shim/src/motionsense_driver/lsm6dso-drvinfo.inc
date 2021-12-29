/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_lsm6dso.h"
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
 * Create driver data. It can be shared among the entries in
 * motion_sensors array which are using the same lsm6dso driver.
 */
#define CREATE_SENSOR_DATA_LSM6DSO(id, drvdata_name) \
	static struct stprivate_data drvdata_name;

/*
 * Create driver data for each lsm6dso drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-lsm6dso")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_lsm6dso, CREATE_SENSOR_DATA_LSM6DSO)
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
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each lsm6dso accel instance(compatible = "cros-ec,lsm6dso-accel")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lsm6dso_accel, MOTIONSENSE_CHIP_LSM6DSO, \
		MOTIONSENSE_TYPE_ACCEL,	lsm6dso_drv, \
		LSM6DSO_ODR_MIN_VAL, LSM6DSO_ODR_MAX_VAL)

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each lsm6dso gyro instance (compatible = "cros-ec,lsm6dso-gyro")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lsm6dso_gyro, MOTIONSENSE_CHIP_LSM6DSO, \
		MOTIONSENSE_TYPE_GYRO, lsm6dso_drv, \
		LSM6DSO_ODR_MIN_VAL, LSM6DSO_ODR_MAX_VAL)
