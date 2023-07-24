/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/als_tcs3400_public.h"

/*
 * CREATE_SENSOR_DATA which is defined in motionsense_sensors.c is
 * the helper to create sensor driver specific data.
 *
 * CREATE_SENSOR_DATA gets two arguments. One is the compatible
 * property value specified in device tree and the other one is the macro
 * that actually creates sensor driver specific data. The macro gets
 * node id and the name to be used for the sensor driver data.
 */

/* include macros for common data strutures from accelgyro.h */
#include "drvdata-accelgyro.h"

/* Create driver data for tcs3400 driver. */
#define CREATE_SENSOR_DATA_TCS3400_CLEAR(id, drvdata_name)		\
	static struct als_drv_data_t drvdata_name =			\
	    ACCELGYRO_ALS_DRV_DATA(DT_CHILD(id, als_drv_data));

/*
 * Create driver data for each TCS3400-clear drvdata instance in device tree.
 * (compatible = "cros-ec,drvdata-tcs3400-clear")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_tcs3400_clear,	\
	CREATE_SENSOR_DATA_TCS3400_CLEAR)

/* driver data for tcs3400 rgb */
#define TCS3400_RGB_SATRURATION(id)					\
		COND_CODE_1(DT_NODE_HAS_PROP(id, again),		\
			(.saturation.again = DT_PROP(id, again),),	\
			(.saturation.again = TCS_DEFAULT_AGAIN,))	\
		COND_CODE_1(DT_NODE_HAS_PROP(id, atime),		\
			(.saturation.again = DT_PROP(id, atime),),	\
			(.saturation.again = TCS_DEFAULT_ATIME,))

#define CREATE_SENSOR_DATA_TCS3400_RGB(id, drvdata_name)		\
	static struct tcs3400_rgb_drv_data_t drvdata_name = {		\
		.calibration = ACCELGYRO_RGB_CALIBRATION(		\
			DT_CHILD(id, rgb_calibration)),			\
		TCS3400_RGB_SATRURATION(DT_CHILD(id, saturation))	\
	};

/*
 * Create driver data for each TCS3400-rgb drvdata instance in device tree.
 * (compatible = "cros-ec,drvdata-tcs3400-rgb")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_tcs3400_rgb, CREATE_SENSOR_DATA_TCS3400_RGB)
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
 * for each TCS3400 clear instance (compatible = "cros-ec,tcs3400-clear")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_tcs3400_clear, MOTIONSENSE_CHIP_TCS3400,	\
		MOTIONSENSE_TYPE_LIGHT, tcs3400_drv,			\
		TCS3400_LIGHT_MIN_FREQ, TCS3400_LIGHT_MAX_FREQ)

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each TCS3400 RGB instance (compatible = "cros-ec,tcs3400-rgb")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_tcs3400_rgb, MOTIONSENSE_CHIP_TCS3400,	\
		MOTIONSENSE_TYPE_LIGHT_RGB, tcs3400_rgb_drv, 0, 0)
