/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "temp_sensor.h"
#include "temp_sensor/pct2075.h"
#include "temp_sensor/sb_tsi.h"
#include "temp_sensor/temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "temp_sensor/tmp112.h"

#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))

#define GET_POWER_GOOD_PROP(node_id) DT_PROP(node_id, power_good_pin)

#define GET_POWER_GOOD_DEV(node_id)					       \
		DEVICE_DT_GET(DT_GPIO_CTLR(GET_POWER_GOOD_PROP(node_id),       \
					   gpios))

#define GET_POWER_GOOD_PIN(node_id) DT_GPIO_PIN(GET_POWER_GOOD_PROP(node_id),  \
						gpios)

#if ANY_INST_HAS_POWER_GOOD_PIN
#define FILL_POWER_GOOD(node_id) \
COND_CODE_1(DT_NODE_HAS_PROP(node_id, power_good_pin),			      \
		(.power_good_dev = GET_POWER_GOOD_DEV(node_id),		      \
		 .power_good_pin = GET_POWER_GOOD_PIN(node_id), ),	      \
		(.power_good_dev = NULL,				      \
		 .power_good_pin = 0, ))
#else
#define FILL_POWER_GOOD(node_id)
#endif /* ANY_INST_HAS_POWER_GOOD_PIN */

static int thermistor_get_temp(const struct temp_sensor_t *sensor,
			       int *temp_ptr)
{
	return thermistor_get_temperature(sensor->idx, temp_ptr,
					  sensor->zephyr_info->thermistor);
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

#define GET_ZEPHYR_TEMP_SENSOR_THERMISTOR(node_id)			      \
	(&(struct zephyr_temp_sensor){					      \
		.read = &thermistor_get_temp,				      \
		.thermistor =                                                 \
			GET_THERMISTOR_INFO(DT_PHANDLE(node_id, thermistor)), \
		FILL_POWER_GOOD(node_id)				      \
	})

#define TEMP_THERMISTOR(node_id)                                              \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {                                   \
		.name = DT_LABEL(node_id),                                    \
		.idx = ZSHIM_ADC_ID(DT_PHANDLE(node_id, adc)),                \
		.type = TEMP_SENSOR_TYPE_BOARD,                               \
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_THERMISTOR(node_id),    \
	},

DT_FOREACH_STATUS_OKAY(cros_ec_thermistor, DEFINE_THERMISTOR_DATA)

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_temp_sensor_pct2075)
static int pct2075_get_temp(const struct temp_sensor_t *sensor, int *temp_ptr)
{
	return pct2075_get_val_k(sensor->idx, temp_ptr);
}
#endif /* cros_ec_temp_sensor_pct2075 */

#define DEFINE_PCT2075_DATA(node_id)                                        \
	[ZSHIM_PCT2075_SENSOR_ID(node_id)] = {                              \
		.i2c_port = I2C_PORT(DT_PHANDLE(node_id, port)),            \
		.i2c_addr_flags = DT_STRING_TOKEN(node_id, i2c_addr_flags), \
	},

#define GET_ZEPHYR_TEMP_SENSOR_PCT2075(node_id)				      \
	(&(struct zephyr_temp_sensor){					      \
		.read = &pct2075_get_temp,				      \
		.thermistor = NULL,                                           \
		FILL_POWER_GOOD(node_id)				      \
	})

#define TEMP_PCT2075(node_id)						\
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {				\
		.name = DT_LABEL(node_id),				\
		.idx = ZSHIM_PCT2075_SENSOR_ID(node_id),		\
		.type = TEMP_SENSOR_TYPE_BOARD,				\
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_PCT2075(node_id),	\
	},

const struct pct2075_sensor_t pct2075_sensors[PCT2075_COUNT] = {
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_pct2075, DEFINE_PCT2075_DATA)
};

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_temp_sensor_sb_tsi)
static int sb_tsi_get_temp(const struct temp_sensor_t *sensor, int *temp_ptr)
{
	return sb_tsi_get_val(sensor->idx, temp_ptr);
}

/* There can be only one SB TSI sensor with current driver */
#if DT_NUM_INST_STATUS_OKAY(cros_ec_temp_sensor_sb_tsi) > 1
#error "Unsupported number of SB TSI sensors"
#endif

#endif /* cros_ec_temp_sensor_sb_tsi */

#define GET_ZEPHYR_TEMP_SENSOR_SB_TSI(node_id)				      \
	(&(struct zephyr_temp_sensor){					      \
		.read = &sb_tsi_get_temp,				      \
		.thermistor = NULL,                                           \
		FILL_POWER_GOOD(node_id)				      \
	})

#define TEMP_SB_TSI(node_id)						\
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {				\
		.name = DT_LABEL(node_id),				\
		.idx = 0,						\
		.type = TEMP_SENSOR_TYPE_CPU,				\
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_SB_TSI(node_id),	\
	},

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

#define GET_ZEPHYR_TEMP_SENSOR_TMP112(node_id)				      \
	(&(struct zephyr_temp_sensor){					      \
		.read = &tmp112_get_temp,				      \
		.thermistor = NULL,                                           \
		FILL_POWER_GOOD(node_id)				      \
	})

#define TEMP_TMP112(node_id)						\
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {				\
		.name = DT_LABEL(node_id),				\
		.idx = ZSHIM_TMP112_SENSOR_ID(node_id),			\
		.type = TEMP_SENSOR_TYPE_BOARD,				\
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_TMP112(node_id),	\
	},

const struct tmp112_sensor_t tmp112_sensors[TMP112_COUNT] = {
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_tmp112, DEFINE_TMP112_DATA)
};

const struct temp_sensor_t temp_sensors[] = {
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_thermistor, TEMP_THERMISTOR)
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_pct2075, TEMP_PCT2075)
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_sb_tsi, TEMP_SB_TSI)
	DT_FOREACH_STATUS_OKAY(cros_ec_temp_sensor_tmp112, TEMP_TMP112)
};

int temp_sensor_read(enum temp_sensor_id id, int *temp_ptr)
{
	const struct temp_sensor_t *sensor;

	if (id < 0 || id >= TEMP_SENSOR_COUNT)
		return EC_ERROR_INVAL;
	sensor = temp_sensors + id;

#if ANY_INST_HAS_POWER_GOOD_PIN
	if (sensor->zephyr_info->power_good_dev) {
		if (!gpio_pin_get(sensor->zephyr_info->power_good_dev,
				  sensor->zephyr_info->power_good_pin))
			return EC_ERROR_NOT_POWERED;
	}
#endif

	return sensor->zephyr_info->read(sensor, temp_ptr);
}

#endif /* named_temp_sensors */
