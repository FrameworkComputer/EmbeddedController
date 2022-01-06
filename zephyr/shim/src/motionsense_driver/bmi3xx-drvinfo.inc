/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_bmi_common_public.h"
#include "driver/accelgyro_bmi3xx.h"

/*
 * CREATE_SENSOR_DATA which is defined in motionsense_sensros.c is
 * the helper to create sensor driver specific data.
 *
 * CREATE_SENSOR_DATA gets two arguments. One is the compatible
 * property value specified in device tree and the other one is the macro
 * that actually creates sensor driver specific data. The macro gets
 * node id and the name to be used for the sensor driver data.
 */

/*
 * Create driver data. It can be shared among the entries in
 * motion_sensors array which are using the same bmi3xx driver.
 */
#define CREATE_SENSOR_DATA_BMI3XX(id, drvdata_name)			\
	static struct bmi_drv_data_t drvdata_name;

/*
 * Create driver data for each BMI3XX drvinfo instance in device tree.
 * (compatible = "cros-ec,drvdata-bmi3xx")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_bmi3xx, CREATE_SENSOR_DATA_BMI3XX)
/*
 * CREATE_MOTION_SENSOR which is defined in motionsense_sensros.c is
 * the macro to create an entry in motion_sensors array.
 * The macro gets value of compatible property of
 * the sensor in device tree and sensor specific values like chip ID,
 * type of sensor, name of driver, default min/max frequency.
 * Then using the values, it creates the corresponding motion_sense_t entry
 * in motion_sensors array.
 */

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each BMI3XX_accel instance(compatible = "cros-ec,bmi3xx-accel")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_bmi3xx_accel, MOTIONSENSE_CHIP_BMI323,	\
		MOTIONSENSE_TYPE_ACCEL,	bmi3xx_drv,			\
		BMI_ACCEL_MIN_FREQ, BMI_ACCEL_MAX_FREQ)

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each BMI3XX_gyro instance (compatible = "cros-ec,bmi3xx-gyro")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_bmi3xx_gyro, MOTIONSENSE_CHIP_BMI323,	\
		MOTIONSENSE_TYPE_GYRO, bmi3xx_drv,			\
		BMI_GYRO_MIN_FREQ, BMI_GYRO_MAX_FREQ)
