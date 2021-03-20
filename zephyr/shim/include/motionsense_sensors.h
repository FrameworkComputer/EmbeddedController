/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MOTIONSENSE_SENSORS_H
#define __CROS_EC_MOTIONSENSE_SENSORS_H

#include <devicetree.h>

#define SENSOR_NODE			DT_PATH(motionsense_sensor)
#define SENSOR_INFO_NODE		DT_PATH(motionsense_sensor_info)

#define SENSOR_ID(id)			DT_CAT(SENSOR_, id)
#define SENSOR_ID_WITH_COMMA(id)					\
	IF_ENABLED(DT_NODE_HAS_STATUS(id, okay), (SENSOR_ID(id),))

enum sensor_id {
#if DT_NODE_EXISTS(SENSOR_NODE)
	DT_FOREACH_CHILD(SENSOR_NODE, SENSOR_ID_WITH_COMMA)
#endif
	SENSOR_COUNT,
};

/*
 * Find the accelerometers for lid angle calculation.
 *
 * The angle calculation requires two accelerometers. One is on the lid
 * and the other one is on the base. So we need to specify which sensor is
 * on the lid and which one is on the base. We use two labels "lid_accel"
 * and "base_accel".
 *
 * base_accel - label for the accelerometer sensor on the base.
 * lid_accel - label for the accelerometer sensor on the lid.
 *
 * e.g) below shows BMA255 is the accelerometer on the lid and bmi260 is
 *     the accelerometer on the base.
 *
 * motionsense-sensor {
 *         lid_accel: lid-accel {
 *                 compatible = "cros-ec,bma255";
 *                 status = "okay";
 *                         :
 *                         :
 *         };
 *
 *	   base_accel: base-accel {
 *                 compatible = "cros-ec,bmi260";
 *                 status = "okay";
 *                      :
 *                      :
 *         };
 * };
 */
#ifdef CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_LID	SENSOR_ID(DT_NODELABEL(lid_accel))
#define CONFIG_LID_ANGLE_SENSOR_BASE	SENSOR_ID(DT_NODELABEL(base_accel))
#endif

#endif /* __CROS_EC_MOTIONSENSE_SENSORS_H */
