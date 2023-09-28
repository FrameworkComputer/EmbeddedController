/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "adc.h"
#include "charger/chg_rt9490.h"
#include "driver/charger/rt9490.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "temp_sensor/f75303.h"
#include "temp_sensor/pct2075.h"
#include "temp_sensor/sb_tsi.h"
#include "temp_sensor/temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "temp_sensor/tmp112.h"

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 29

#if DT_HAS_COMPAT_STATUS_OKAY(TEMP_SENSORS_COMPAT)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(TEMP_SENSORS_COMPAT) == 1,
	     "Only one temperature sensors node is allowed");

#define GET_POWER_GOOD_PROP(node_id) DT_PROP(node_id, power_good_pin)

#define GET_POWER_GOOD_DEV(node_id) \
	DEVICE_DT_GET(DT_GPIO_CTLR(GET_POWER_GOOD_PROP(node_id), gpios))

#define GET_POWER_GOOD_PIN(node_id) \
	DT_GPIO_PIN(GET_POWER_GOOD_PROP(node_id), gpios)

#define POWER_GOOD_ENTRY(node_id)                      \
	.power_good_dev = GET_POWER_GOOD_DEV(node_id), \
	.power_good_pin = GET_POWER_GOOD_PIN(node_id),

#define POWER_GOOD_ENTRY_NULL(node_id) \
	.power_good_dev = NULL, .power_good_pin = 0,

#define POWER_GOOD(node_id)                                \
	[TEMP_SENSOR_ID(node_id)] = { COND_CODE_1(         \
		DT_NODE_HAS_PROP(node_id, power_good_pin), \
		(POWER_GOOD_ENTRY(node_id)),               \
		(POWER_GOOD_ENTRY_NULL(node_id))) }

#if ANY_INST_HAS_POWER_GOOD_PIN
#define FILL_POWER_GOOD(node_id)                                       \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, power_good_pin),         \
		    (.power_good_dev = GET_POWER_GOOD_DEV(node_id),    \
		     .power_good_pin = GET_POWER_GOOD_PIN(node_id), ), \
		    (.power_good_dev = NULL, .power_good_pin = 0, ))
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
				    .temp = DT_PROP(node_sample_id, temp) }

#define DEFINE_THERMISTOR_DATA(node_id)                                   \
	static const struct thermistor_data_pair DT_CAT(                  \
		node_id, _thermistor_data)[] = {                          \
		DT_FOREACH_CHILD_SEP(node_id, GET_THERMISTOR_DATUM, (, )) \
	};

#define GET_THERMISTOR_INFO(node_id)                                \
	(&(const struct thermistor_info){                           \
		.scaling_factor = DT_PROP(node_id, scaling_factor), \
		.num_pairs = DT_PROP(node_id, num_pairs),           \
		.data = DT_CAT(node_id, _thermistor_data),          \
	})

#define GET_ZEPHYR_TEMP_SENSOR_THERMISTOR(named_id, sensor_id) \
	(&(const struct zephyr_temp_sensor){                   \
		.read = &thermistor_get_temp,                  \
		.thermistor = GET_THERMISTOR_INFO(             \
			DT_PHANDLE(sensor_id, thermistor)),    \
		.update_temperature = NULL,                    \
		FILL_POWER_GOOD(named_id) })

#define TEMP_THERMISTOR(named_id, sensor_id)                                 \
	[TEMP_SENSOR_ID(named_id)] = {                                       \
		.name = DT_NODE_FULL_NAME(sensor_id),                        \
		.idx = ZSHIM_ADC_ID(DT_PHANDLE(sensor_id, adc)),             \
		.type = TEMP_SENSOR_TYPE_BOARD,                              \
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_THERMISTOR(named_id,   \
								 sensor_id), \
	}

DT_FOREACH_STATUS_OKAY(cros_ec_thermistor, DEFINE_THERMISTOR_DATA)

#if DT_HAS_COMPAT_STATUS_OKAY(PCT2075_COMPAT)
/* The function maybe unused because a temperature sensor can be added to dts
 * without a reference in the cros_ec_temp_sensors node.
 */
__maybe_unused static int pct2075_get_temp(const struct temp_sensor_t *sensor,
					   int *temp_ptr)
{
	return pct2075_get_val_k(sensor->idx, temp_ptr);
}
#endif /* PCT2075_COMPAT */

#define DEFINE_PCT2075_DATA(node_id)                                  \
	[PCT2075_SENSOR_ID(node_id)] = {                              \
		.i2c_port = I2C_PORT_BY_DEV(node_id),                 \
		.i2c_addr_flags =                                     \
			(DT_REG_ADDR(node_id) | I2C_FLAG_BIG_ENDIAN), \
	},

#define GET_ZEPHYR_TEMP_SENSOR_PCT2075(named_id)                  \
	(&(const struct zephyr_temp_sensor){                      \
		.read = &pct2075_get_temp,                        \
		.thermistor = NULL,                               \
		.update_temperature = pct2075_update_temperature, \
		FILL_POWER_GOOD(named_id) })

#define TEMP_PCT2075(named_id, sensor_id)                                \
	[TEMP_SENSOR_ID(named_id)] = {                                   \
		.name = DT_NODE_FULL_NAME(sensor_id),                    \
		.idx = PCT2075_SENSOR_ID(sensor_id),                     \
		.type = TEMP_SENSOR_TYPE_BOARD,                          \
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_PCT2075(named_id), \
	}

const struct pct2075_sensor_t pct2075_sensors[PCT2075_COUNT] = {
	DT_FOREACH_STATUS_OKAY(PCT2075_COMPAT, DEFINE_PCT2075_DATA)
};

#if DT_HAS_COMPAT_STATUS_OKAY(SB_TSI_COMPAT)
/* The function maybe unused because a temperature sensor can be added to dts
 * without a reference in the cros_ec_temp_sensors node.
 */
__maybe_unused static int sb_tsi_get_temp(const struct temp_sensor_t *sensor,
					  int *temp_ptr)
{
	return sb_tsi_get_val(sensor->idx, temp_ptr);
}

/* There can be only one SB TSI sensor with current driver */
#if DT_NUM_INST_STATUS_OKAY(SB_TSI_COMPAT) > 1
#error "Unsupported number of SB TSI sensors"
#endif

#endif /* SB_TSI_COMPAT */

#define GET_ZEPHYR_TEMP_SENSOR_SB_TSI(named_id)                          \
	(&(const struct zephyr_temp_sensor){ .read = &sb_tsi_get_temp,   \
					     .thermistor = NULL,         \
					     .update_temperature = NULL, \
					     FILL_POWER_GOOD(named_id) })

#define TEMP_SB_TSI(named_id, sensor_id)                                \
	[TEMP_SENSOR_ID(named_id)] = {                                  \
		.name = DT_NODE_FULL_NAME(sensor_id),                   \
		.idx = 0,                                               \
		.type = TEMP_SENSOR_TYPE_CPU,                           \
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_SB_TSI(named_id), \
	}

#if DT_HAS_COMPAT_STATUS_OKAY(TMP112_COMPAT)
/* The function maybe unused because a temperature sensor can be added to dts
 * without a reference in the cros_ec_temp_sensors node.
 */
__maybe_unused static int tmp112_get_temp(const struct temp_sensor_t *sensor,
					  int *temp_ptr)
{
	return tmp112_get_val_k(sensor->idx, temp_ptr);
}
#endif /* TMP112_COMPAT */

#define DEFINE_TMP112_DATA(node_id)                     \
	[TMP112_SENSOR_ID(node_id)] = {                 \
		.i2c_port = I2C_PORT_BY_DEV(node_id),   \
		.i2c_addr_flags = DT_REG_ADDR(node_id), \
	},

#define GET_ZEPHYR_TEMP_SENSOR_TMP112(named_id)                  \
	(&(const struct zephyr_temp_sensor){                     \
		.read = &tmp112_get_temp,                        \
		.thermistor = NULL,                              \
		.update_temperature = tmp112_update_temperature, \
		FILL_POWER_GOOD(named_id) })

#define TEMP_TMP112(named_id, sensor_id)                                \
	[TEMP_SENSOR_ID(named_id)] = {                                  \
		.name = DT_NODE_FULL_NAME(sensor_id),                   \
		.idx = TMP112_SENSOR_ID(sensor_id),                     \
		.type = TEMP_SENSOR_TYPE_BOARD,                         \
		.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_TMP112(named_id), \
	}

const struct tmp112_sensor_t tmp112_sensors[TMP112_COUNT] = {
	DT_FOREACH_STATUS_OKAY(TMP112_COMPAT, DEFINE_TMP112_DATA)
};

#if DT_HAS_COMPAT_STATUS_OKAY(F75303_COMPAT)
/* The function maybe unused because a temperature sensor can be added to dts
 * without a reference in the cros_ec_temp_sensors node.
 */
__maybe_unused static int f75303_get_temp(const struct temp_sensor_t *sensor,
					  int *temp_ptr)
{
	return f75303_get_val_k(sensor->idx, temp_ptr);
}
#endif /* f75303_COMPAT */

#define DEFINE_F75303_DATA(sensor_id)                     \
	[F75303_SENSOR_ID(sensor_id)] = {                 \
		.i2c_port = I2C_PORT_BY_DEV(sensor_id),   \
		.i2c_addr_flags = DT_REG_ADDR(sensor_id), \
	},

#define GET_ZEPHYR_TEMP_SENSOR_F75303(named_id, sensor_id)       \
	(&(const struct zephyr_temp_sensor){                     \
		.read = &f75303_get_temp,                        \
		.thermistor = NULL,                              \
		.update_temperature = f75303_update_temperature, \
		FILL_POWER_GOOD(named_id) })

#define TEMP_F75303(named_id, sensor_id)                                    \
	[TEMP_SENSOR_ID(named_id)] = {                                      \
		.name = DT_NODE_FULL_NAME(sensor_id),                       \
		.idx = F75303_SENSOR_ID(sensor_id),                         \
		.type = TEMP_SENSOR_TYPE_BOARD,                             \
		.zephyr_info =                                              \
			GET_ZEPHYR_TEMP_SENSOR_F75303(named_id, sensor_id), \
	}

const struct f75303_sensor_t f75303_sensors[F75303_IDX_COUNT] = {
	DT_FOREACH_STATUS_OKAY(F75303_COMPAT, DEFINE_F75303_DATA)
};

/* There can be only one thermistor on RT9490 with current driver */
#define ADD_ONE(node_id) 1 +
#if DT_FOREACH_STATUS_OKAY_VARGS(RT9490_CHG_COMPAT, TEMP_RT9490_FN, \
				 ADD_ONE) 0 > 1
#error "Unsupported number of thermistor on RT9490"
#endif
#undef ADD_ONE

#define GET_ZEPHYR_TEMP_SENSOR_RT9490(named_id, sensor_id)  \
	(&(const struct zephyr_temp_sensor){                \
		.read = &rt9490_get_thermistor_val,         \
		.thermistor = GET_THERMISTOR_INFO(          \
			DT_PHANDLE(sensor_id, thermistor)), \
		.update_temperature = NULL,                 \
		FILL_POWER_GOOD(named_id) })

#define TEMP_RT9490(named_id, sensor_id) \
	COND_CODE_1(DT_NODE_HAS_PROP(sensor_id, thermistor), (                 \
		[TEMP_SENSOR_ID(named_id)] = {                                 \
			.name = DT_NODE_FULL_NAME(sensor_id),                  \
			.idx = 0,                                              \
			.type = TEMP_SENSOR_TYPE_BOARD,                        \
			.zephyr_info = GET_ZEPHYR_TEMP_SENSOR_RT9490(named_id, \
				       sensor_id),                             \
		} ), ())

#define DT_DRV_COMPAT TEMP_SENSORS_COMPAT

#define CHECK_COMPAT(compat, named_id, sensor_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(sensor_id, compat),   \
		    (config_fn(named_id, sensor_id)), ())

#define TEMP_SENSOR_FIND(named_id, sensor_id)                                 \
	CHECK_COMPAT(THERMISTOR_COMPAT, named_id, sensor_id, TEMP_THERMISTOR) \
	CHECK_COMPAT(PCT2075_COMPAT, named_id, sensor_id, TEMP_PCT2075)       \
	CHECK_COMPAT(SB_TSI_COMPAT, named_id, sensor_id, TEMP_SB_TSI)         \
	CHECK_COMPAT(TMP112_COMPAT, named_id, sensor_id, TEMP_TMP112)         \
	CHECK_COMPAT(RT9490_CHG_COMPAT, named_id, sensor_id, TEMP_RT9490)     \
	CHECK_COMPAT(F75303_COMPAT, named_id, sensor_id, TEMP_F75303)

#define TEMP_SENSOR_ENTRY(named_id) \
	TEMP_SENSOR_FIND(named_id, DT_PHANDLE(named_id, sensor))

const struct temp_sensor_t temp_sensors[] = { DT_FOREACH_CHILD_SEP(
	TEMP_SENSORS_NODEID, TEMP_SENSOR_ENTRY, (, )) };

BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static bool temp_sensor_check_power(const struct temp_sensor_t *sensor)
{
#if ANY_INST_HAS_POWER_GOOD_PIN
	if (sensor->zephyr_info->power_good_dev) {
		if (!gpio_pin_get(sensor->zephyr_info->power_good_dev,
				  sensor->zephyr_info->power_good_pin))
			return false;
	}
#endif
	return true;
}

int temp_sensor_read(enum temp_sensor_id id, int *temp_ptr)
{
	const struct temp_sensor_t *sensor;

	if (id < 0 || id >= TEMP_SENSOR_COUNT)
		return EC_ERROR_INVAL;
	sensor = temp_sensors + id;

	if (!temp_sensor_check_power(sensor))
		return EC_ERROR_NOT_POWERED;

	return sensor->zephyr_info->read(sensor, temp_ptr);
}

void temp_sensors_update(void)
{
	for (int i = 0; i < TEMP_SENSOR_COUNT; i++) {
		const struct temp_sensor_t *sensor = temp_sensors + i;

		if (!sensor->zephyr_info->update_temperature)
			continue;

		if (!temp_sensor_check_power(sensor))
			continue;

		sensor->zephyr_info->update_temperature(sensor->idx);
	}
}
DECLARE_HOOK(HOOK_SECOND, temp_sensors_update, HOOK_PRIO_TEMP_SENSOR);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(TEMP_SENSORS_COMPAT) */
