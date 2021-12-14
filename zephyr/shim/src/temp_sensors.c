/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "temp_sensor/tmp112.h"

#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
static int thermistor_get_temp(const struct temp_sensor_t *sensor,
			       int *temp_ptr)
{
	return thermistor_get_temperature(sensor->idx, temp_ptr,
					  sensor->thermistor);
}

#define GET_THERMISTOR_DATUM(node_sample_id)                                 \
	[DT_PROP(node_sample_id,                                             \
		 sample_index)] = { .mv = DT_PROP(node_sample_id, milivolt), \
				    .temp = DT_PROP(node_sample_id, temp) },

#define DEFINE_THERMISTOR_DATA(node_id)                         \
	static const struct thermistor_data_pair DT_CAT(        \
		node_id, _thermistor_data)[] = {                \
		DT_FOREACH_CHILD(node_id, GET_THERMISTOR_DATUM) \
	};

#define GET_THERMISTOR_INFO(node_id)                                \
	(&(struct thermistor_info){                                 \
		.scaling_factor = DT_PROP(node_id, scaling_factor), \
		.num_pairs = DT_PROP(node_id, num_pairs),           \
		.data = DT_CAT(node_id, _thermistor_data),          \
	})

#define TEMP_THERMISTOR(node_id)                                              \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {                                   \
		.name = DT_LABEL(node_id),                                    \
		.read = &thermistor_get_temp,                                 \
		.idx = ZSHIM_ADC_ID(DT_PHANDLE(node_id, adc)),                \
		.type = TEMP_SENSOR_TYPE_BOARD,                               \
		.thermistor =                                                 \
			GET_THERMISTOR_INFO(DT_PHANDLE(node_id, thermistor)), \
	},

DT_FOREACH_STATUS_OKAY(cros_ec_thermistor, DEFINE_THERMISTOR_DATA)

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_temp_sensor_tmp112)
static int tmp112_get_temp(const struct temp_sensor_t *sensor, int *temp_ptr)
{
	return tmp112_get_val_k(sensor->idx, temp_ptr);
}
#endif /* cros_ec_temp_sensor_tmp112 */

#define DEFINE_TMP112_DATA(node_id)                                         \
	[ZSHIM_TMP112_SENSOR_ID(node_id)] = {                               \
		.i2c_port = I2C_PORT(DT_PHANDLE(node_id, port)),            \
		.i2c_addr_flags = DT_STRING_TOKEN(node_id, i2c_addr_flags), \
	},

#define TEMP_TMP112(node_id)                            \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {             \
		.name = DT_LABEL(node_id),              \
		.read = tmp112_get_temp,                \
		.idx = ZSHIM_TMP112_SENSOR_ID(node_id), \
		.type = TEMP_SENSOR_TYPE_BOARD,         \
	},

const struct tmp112_sensor_t tmp112_sensors[TMP112_COUNT] = {
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_tmp112, DEFINE_TMP112_DATA)
};

const struct temp_sensor_t temp_sensors[] = {
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_thermistor, TEMP_THERMISTOR)
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_tmp112, TEMP_TMP112)
};
#endif /* named_temp_sensors */
