/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "adc.h"
#include "../driver/temp_sensor/thermistor.h"

#define TEMP_THERMISTOR(node_id, fn)             \
	[node_id] = {                            \
		.name = DT_LABEL(node_id),       \
		.read = fn,                      \
		.idx = DT_PHANDLE(node_id, adc), \
		.type = TEMP_SENSOR_TYPE_BOARD,  \
	},

#define TEMP_3V3_30K9_47K_4050B(node_id) \
	TEMP_THERMISTOR(node_id, get_temp_3v3_30k9_47k_4050b)

#define TEMP_3V0_22K6_47K_4050B(node_id) \
	TEMP_THERMISTOR(node_id, get_temp_3v0_22k6_47k_4050b)

#define TEMP_3V3_51K1_47K_4050B(node_id) \
	TEMP_THERMISTOR(node_id, get_temp_3v3_51k1_47k_4050b)

#define TEMP_3V3_13K7_47K_4050B(node_id) \
	TEMP_THERMISTOR(node_id, get_temp_3v3_13k7_47k_4050b)

#define TEMP_DEVICE_INST(inst, compat, expr) expr(DT_INST(inst, compat))

#define TEMP_DEVICE(compat, expr)                                       \
	UTIL_LISTIFY(DT_NUM_INST_STATUS_OKAY(compat), TEMP_DEVICE_INST, \
		     compat, expr)

#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))

const struct temp_sensor_t temp_sensors[] = {
	TEMP_DEVICE(temp_3v3_13k7_47k_4050b, TEMP_3V3_13K7_47K_4050B)
	TEMP_DEVICE(temp_3v3_51k1_47k_4050b, TEMP_3V3_51K1_47K_4050B)
	TEMP_DEVICE(temp_3v0_22k6_47k_4050b, TEMP_3V0_22K6_47K_4050B)
	TEMP_DEVICE(temp_3v3_30k9_47k_4050b, TEMP_3V3_30K9_47K_4050B)
};

#endif /* named_temp_sensors */
