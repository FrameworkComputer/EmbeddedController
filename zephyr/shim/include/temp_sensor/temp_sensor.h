/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_
#define ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_

#include <devicetree.h>

#ifdef CONFIG_PLATFORM_EC_TEMP_SENSOR
#define NODE_ID_AND_COMMA(node_id) node_id,
enum temp_sensor_id {
#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
	DT_FOREACH_CHILD(DT_PATH(named_temp_sensors), NODE_ID_AND_COMMA)
#endif /* named_temp_sensors */
	TEMP_SENSOR_COUNT
};
#endif /* CONFIG_PLATFORM_EC_TEMP_SENSOR */

#endif /* ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_ */
