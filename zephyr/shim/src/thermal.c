/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "ec_commands.h"

#define THERMAL_CONFIG(node_id) \
	[TEMP_SENSOR_ID(node_id)] = {				\
		.temp_host = {						\
			[EC_TEMP_THRESH_WARN] =			\
			C_TO_K(DT_PROP_OR(node_id,			\
					  temp_host_warn,		\
					  -273)),			\
			[EC_TEMP_THRESH_HIGH] =			\
			C_TO_K(DT_PROP_OR(node_id,			\
					  temp_host_high,		\
					  -273)),			\
			[EC_TEMP_THRESH_HALT] =			\
			C_TO_K(DT_PROP_OR(node_id,			\
					  temp_host_halt,		\
					  -273)),			\
		},							\
		.temp_host_release = {					\
			[EC_TEMP_THRESH_WARN] = C_TO_K(		\
				DT_PROP_OR(node_id,			\
					   temp_host_release_warn,	\
					   -273)),			\
			[EC_TEMP_THRESH_HIGH] = C_TO_K(		\
				DT_PROP_OR(node_id,			\
					   temp_host_release_high,	\
					   -273)),			\
			[EC_TEMP_THRESH_HALT] = C_TO_K(		\
				DT_PROP_OR(node_id,			\
					   temp_host_release_halt,	\
					   -273)),			\
		},							\
		.temp_fan_off = C_TO_K(DT_PROP_OR(node_id,		\
						  temp_fan_off,	\
						  -273)),		\
		.temp_fan_max = C_TO_K(DT_PROP_OR(node_id,		\
						  temp_fan_max,	\
						  -273)),		\
	}

struct ec_thermal_config thermal_params[] = {
#if DT_HAS_COMPAT_STATUS_OKAY(TEMP_SENSORS_COMPAT)
	DT_FOREACH_CHILD_SEP(TEMP_SENSORS_NODEID, THERMAL_CONFIG, (, ))
#endif /* DT_HAS_COMPAT_STATUS_OKAY(TEMP_SENSORS_COMPAT) */
};
