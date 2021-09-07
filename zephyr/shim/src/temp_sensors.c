/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "adc.h"
#include "temp_sensor/thermistor.h"

#define TEMP_THERMISTOR(node_id)                                 \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {                      \
		.name = DT_LABEL(node_id),                       \
		.read = DT_STRING_TOKEN(node_id, get_temp_func), \
		.idx = ZSHIM_ADC_ID(DT_PHANDLE(node_id, adc)),   \
		.type = TEMP_SENSOR_TYPE_BOARD,                  \
	},

#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
const struct temp_sensor_t temp_sensors[] = {
	DT_FOREACH_CHILD(DT_PATH(named_temp_sensors), TEMP_THERMISTOR)
};
#endif /* named_temp_sensors */
