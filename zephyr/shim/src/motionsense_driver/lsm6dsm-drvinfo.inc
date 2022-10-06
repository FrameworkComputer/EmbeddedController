/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_lsm6dsm.h"
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
 * Create driver data. This must be a separate instance for
 * each entry of the lsm6dsm device in the motion_sensors array.
 */
#define CREATE_SENSOR_DATA_LSM6DSM(id, drvdata_name) \
	static struct lsm6dsm_data drvdata_name = LSM6DSM_DATA;

/*
 * Create driver data for each lsm6dsm drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-lsm6dsm")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_lsm6dsm, CREATE_SENSOR_DATA_LSM6DSM)
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
 * for each lsm6dsm accel instance(compatible = "cros-ec,lsm6dsm-accel")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lsm6dsm_accel, MOTIONSENSE_CHIP_LSM6DSM, \
		MOTIONSENSE_TYPE_ACCEL,	lsm6dsm_drv, \
		LSM6DSM_ODR_MIN_VAL, LSM6DSM_ODR_MAX_VAL)

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each lsm6dsm gyro instance (compatible = "cros-ec,lsm6dsm-gyro")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_lsm6dsm_gyro, MOTIONSENSE_CHIP_LSM6DSM, \
		MOTIONSENSE_TYPE_GYRO, lsm6dsm_drv, \
		LSM6DSM_ODR_MIN_VAL, LSM6DSM_ODR_MAX_VAL)
