/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* APL6012 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "temp_sensor.h"
#include "temp_sensor/apl6012.h"
#include "temp_sensor/temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "util.h"

static int temps[APL6012_IDX_COUNT];

static int raw_read8(int sensor, const int offset, int *data)
{
	return i2c_read8(apl6012_sensors[sensor].i2c_port,
			 apl6012_sensors[sensor].i2c_addr_flags, offset, data);
}

static int get_ch_val(int sensor, const int offset, int *val)
{
	int rv;
	int val_raw = 0;

	rv = raw_read8(sensor, offset, &val_raw);
	if (rv != 0)
		return rv;

	*val = val_raw;
	return EC_SUCCESS;
}

int apl6012_get_val_k(int idx, int *temp_k_ptr)
{
	if (idx < 0 || idx >= APL6012_IDX_COUNT)
		return EC_ERROR_INVAL;

	*temp_k_ptr = MILLI_KELVIN_TO_KELVIN(temps[idx]);
	return EC_SUCCESS;
}

static const struct temp_sensor_t *find_temp_sensor_by_idx(int idx)
{
	int i;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		if (temp_sensors[i].idx == idx) {
			return &temp_sensors[i];
		}
	}

	return NULL;
}

void apl6012_update_temperature(int idx)
{
	int rv;
	uint16_t mv;
	int ch_val;
	int temp_c;
	const struct temp_sensor_t *sensor = find_temp_sensor_by_idx(idx);

	if (!sensor)
		return;

	rv = get_ch_val(idx, (APL6012_TD1 + idx), &ch_val);
	if (rv != 0)
		return;

	/* Vin = (10mV * REG_ch_val) + 1V */
	mv = ch_val * APL6012_MV_STEP + APL6012_MV_OFFSET;
	temp_c = thermistor_linear_interpolate(mv,
					       sensor->zephyr_info->thermistor);
	temps[idx] = CELSIUS_TO_MILLI_KELVIN(temp_c);
}
