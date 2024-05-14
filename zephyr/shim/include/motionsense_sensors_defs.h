/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MOTIONSENSE_SENSORS_DEFS_H
#define __CROS_EC_MOTIONSENSE_SENSORS_DEFS_H

#include "common.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_ID(id) DT_CAT(SENSOR_, id)

/* Define the SENSOR_ID if:
 * DT_NODE_HAS_STATUS(id, okay) && !DT_NODE_HAS_PROP(id, alternate_for)
 */
#define SENSOR_ID_WITH_COMMA(id)                                     \
	IF_ENABLED(DT_NODE_HAS_STATUS(id, okay),                     \
		   (COND_CODE_0(DT_NODE_HAS_PROP(id, alternate_for), \
				(SENSOR_ID(id), ), ())))

/* clang-format off */
enum sensor_id {
#if DT_NODE_EXISTS(SENSOR_NODE)
	DT_FOREACH_CHILD(SENSOR_NODE, SENSOR_ID_WITH_COMMA)
#endif
	SENSOR_COUNT,
};
/* clang-format on */

#undef SENSOR_ID_WITH_COMMA
/* Define the SENSOR_ID if:
 * DT_NODE_HAS_STATUS(id, okay) && DT_NODE_HAS_PROP(id, alternate_for)
 */
#define SENSOR_ID_WITH_COMMA(id)                                     \
	IF_ENABLED(DT_NODE_HAS_STATUS(id, okay),                     \
		   (COND_CODE_1(DT_NODE_HAS_PROP(id, alternate_for), \
				(SENSOR_ID(id), ), ())))
/* clang-format off */
enum sensor_alt_id {
#if DT_NODE_EXISTS(SENSOR_ALT_NODE)
	DT_FOREACH_CHILD(SENSOR_ALT_NODE, SENSOR_ID_WITH_COMMA)
#endif
	SENSOR_ALT_COUNT,
};
/* clang-format on */

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
#define CONFIG_LID_ANGLE_SENSOR_LID SENSOR_ID(DT_NODELABEL(lid_accel))
#define CONFIG_LID_ANGLE_SENSOR_BASE SENSOR_ID(DT_NODELABEL(base_accel))
#endif

/*
 * Get the sensors running in force mode from DT and create a bit mask for it.
 *
 * e.g) lid accel and als_clear are in accel_force_mode. The macro below finds
 *      the corresponding bit for each sensor in bit mask and set it.
 * motionsense-sensor-info {
 *        compatible = "cros-ec,motionsense-sensor-info";
 *
 *        // list of sensors in force mode
 *        accel-force-mode-sensors = <&lid_accel &als_clear>;
 * };
 */
#if DT_NODE_HAS_PROP(SENSOR_INFO_NODE, accel_force_mode_sensors)
#define SENSOR_IN_FORCE_MODE(i, id) \
	| BIT(SENSOR_ID(DT_PHANDLE_BY_IDX(id, accel_force_mode_sensors, i)))
#define CONFIG_ACCEL_FORCE_MODE_MASK                                        \
	(0 LISTIFY(DT_PROP_LEN(SENSOR_INFO_NODE, accel_force_mode_sensors), \
		   SENSOR_IN_FORCE_MODE, (), SENSOR_INFO_NODE))
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MOTIONSENSE_SENSORS_DEFS_H */
