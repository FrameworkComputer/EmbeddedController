/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/als_cm36781.h"

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

/* Create driver data for cm36781 driver. */
#define CREATE_SENSOR_DATA_CM36781(id, drvdata_name)		\
	static struct als_drv_data_t drvdata_name =			\
	    ACCELGYRO_ALS_DRV_DATA(DT_CHILD(id, als_drv_data));

/*
 * Create driver data for each CM36781 drvdata instance in device tree.
 * (compatible = "cros-ec,drvdata-cm36781")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_cm36781,	\
	CREATE_SENSOR_DATA_CM36781)

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each cm36781 clear instance (compatible = "cros-ec,cm36781")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_cm36781, MOTIONSENSE_CHIP_CM36781,	\
		MOTIONSENSE_TYPE_LIGHT, cm36781_drv,			\
		CM36781_MIN_FREQ, CM36781_MAX_FREQ)

