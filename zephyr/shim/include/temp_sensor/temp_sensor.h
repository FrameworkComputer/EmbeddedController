/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_
#define ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_

#include <devicetree.h>

#ifdef CONFIG_PLATFORM_EC_TEMP_SENSOR

#define ZSHIM_TEMP_SENSOR_ID(node_id) DT_STRING_UPPER_TOKEN(node_id, enum_name)
#define TEMP_SENSOR_ID_WITH_COMMA(node_id) ZSHIM_TEMP_SENSOR_ID(node_id),

enum temp_sensor_id {
#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
	DT_FOREACH_CHILD(DT_PATH(named_temp_sensors),
			 TEMP_SENSOR_ID_WITH_COMMA)
#endif /* named_temp_sensors */
	TEMP_SENSOR_COUNT
};

#undef TEMP_SENSOR_ID_WITH_COMMA

/* PCT2075 access array */
#define ZSHIM_PCT2075_SENSOR_ID(node_id) DT_STRING_UPPER_TOKEN(node_id, \
							       pct2075_name)
#define PCT2075_SENSOR_ID_WITH_COMMA(node_id) ZSHIM_PCT2075_SENSOR_ID(node_id),

enum pct2075_sensor {
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_temp_sensor_pct2075)
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_pct2075,
			       PCT2075_SENSOR_ID_WITH_COMMA)
#endif
	PCT2075_COUNT,
};

#undef PCT2075_SENSOR_ID_WITH_COMMA

/* TMP112 access array */
#define ZSHIM_TMP112_SENSOR_ID(node_id) DT_STRING_UPPER_TOKEN(node_id, \
							      tmp112_name)
#define TMP112_SENSOR_ID_WITH_COMMA(node_id) ZSHIM_TMP112_SENSOR_ID(node_id),

enum tmp112_sensor {
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_temp_sensor_tmp112)
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_tmp112,
			       TMP112_SENSOR_ID_WITH_COMMA)
#endif
	TMP112_COUNT,
};

#undef TMP112_SENSOR_ID_WITH_COMMA

#endif /* CONFIG_PLATFORM_EC_TEMP_SENSOR */

#endif /* ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_ */
