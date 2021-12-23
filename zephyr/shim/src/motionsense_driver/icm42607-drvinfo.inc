/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm42607.h"

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
 * motion_sensors array which are using the same icm42607 driver.
 */
#define CREATE_SENSOR_DATA_ICM42607(id, drvdata_name) \
	static struct icm_drv_data_t drvdata_name;

/*
 * Create driver data for each icm42607 drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-icm42607")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_icm42607, CREATE_SENSOR_DATA_ICM42607)
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
 * for each icm42607 accel instance(compatible = "cros-ec,icm42607-accel")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_icm42607_accel, MOTIONSENSE_CHIP_ICM42607, \
		MOTIONSENSE_TYPE_ACCEL,	icm42607_drv, \
		ICM42607_ACCEL_MIN_FREQ, ICM42607_ACCEL_MAX_FREQ)

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each icm42607 gyro instance (compatible = "cros-ec,icm42607-gyro")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_icm42607_gyro, MOTIONSENSE_CHIP_ICM42607, \
		MOTIONSENSE_TYPE_GYRO, icm42607_drv, \
		ICM42607_GYRO_MIN_FREQ, ICM42607_GYRO_MAX_FREQ)
