/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MOTIONSENSE_SENSORS_H
#define __CROS_EC_MOTIONSENSE_SENSORS_H

#include <devicetree.h>
#include "motion_sense.h"

extern struct motion_sensor_t motion_sensors_alt[];

/*
 * Performs probing of an alternate sensor.
 * @param alt_idx Index in motion_sensors_alt of the sensor to be probed.
 *		  It should be gained with SENSOR_ID,
 *		  e.g. with SENSOR_ID(DT_NODELABEL(label)).
 * @return EC_SUCCESS if the probe was successful, non-zero otherwise.
 */
int motion_sense_probe(enum sensor_alt_id alt_idx);

#define ENABLE_ALT_MOTION_SENSOR(alt_id) \
	motion_sensors[SENSOR_ID(DT_PHANDLE(alt_id, alternate_for))] = \
		motion_sensors_alt[SENSOR_ID(alt_id)];

/*
 * Replaces a default motion sensor with an alternate one pointed by nodelabel.
 */
#define MOTIONSENSE_ENABLE_ALTERNATE(nodelabel)                              \
	do {                                                                 \
		BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(nodelabel)),        \
			     "Motionsense alternate node does not exist");   \
		ENABLE_ALT_MOTION_SENSOR(DT_NODELABEL(nodelabel));           \
	} while (0)

/*
 * Probes and replaces a default motion sensor with an alternate one pointed by
 * nodelabel, if the probe was successful.
 */
#define MOTIONSENSE_PROBE_AND_ENABLE_ALTERNATE(nodelabel)                    \
	do {                                                                 \
		BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(nodelabel)),        \
			     "Motionsense alternate node does not exist");   \
		if (!motion_sense_probe(SENSOR_ID(DT_NODELABEL(nodelabel)))) \
			ENABLE_ALT_MOTION_SENSOR(DT_NODELABEL(nodelabel));   \
	} while (0)

#endif /* __CROS_EC_MOTIONSENSE_SENSORS_H */
