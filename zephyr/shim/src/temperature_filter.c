/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "math_util.h"
#include "temperature_filter.h"
#include "thermal.h"
#include "util.h"

#define Q_SCALE 14
/* scale input up to improve filter performance */
#define IN_SCALE 7

struct temperature_filter_t {
	int32_t state[4];
	int32_t coeff[6];
} filters[TEMP_SENSOR_COUNT];

static void temperature_filter_reset(enum temp_sensor_id id)
{
	memset(&filters[id].state, 30 << IN_SCALE, sizeof(filters[id].state));
}

static int temperature_filter_update(enum temp_sensor_id id, int value)
{
	/* we can only accept a range in INT8*/
	value = MIN(value, INT8_MAX);
	value = MAX(value, INT8_MIN);

	int out_scaled =
		filters[id].coeff[0] * (value << IN_SCALE) +
		filters[id].coeff[1] * filters[id].state[0] +
		filters[id].coeff[2] * filters[id].state[1] -
		filters[id].coeff[4] * filters[id].state[2] -
		filters[id].coeff[5] * filters[id].state[3];
	int out = out_scaled >> Q_SCALE;
	/* update delay line */
	filters[id].state[1] = filters[id].state[0];
	filters[id].state[3] = filters[id].state[2];
	filters[id].state[0] = value << IN_SCALE;
	filters[id].state[2] = out;

	return out >> IN_SCALE;
}

static int temperature_filter_get(enum temp_sensor_id id)
{
	return filters[id].state[2] >> IN_SCALE;
}

bool temperature_filter_is_support(enum temp_sensor_id id)
{
	int zero[6] = {0};
	int rv;

	rv = memcmp(thermal_params[id].coefficients, zero, 6);

	/* No coefficients (zero arrays), return false */
	return !!rv;
}

void temperature_filter_get_temp(enum temp_sensor_id id, int *temp_ptr)
{
	*temp_ptr = C_TO_K(temperature_filter_get(id));
}

void temperature_filter_update_temp(enum temp_sensor_id id, int temp_ptr)
{
	/* The virtual temp is over range if we use the Kelvin */
	temperature_filter_update(id, K_TO_C(temp_ptr));
}

static void temperature_filter_init(void)
{
	for (int temp_id = 0; temp_id < TEMP_SENSOR_COUNT; temp_id++) {
		if (temperature_filter_is_support(temp_id))
			memcpy(filters[temp_id].coeff, thermal_params[temp_id].coefficients,
					sizeof(filters[temp_id].coeff));
	}
}
DECLARE_HOOK(HOOK_INIT, temperature_filter_init, HOOK_PRIO_DEFAULT);

static void temperature_filter_resume(void)
{
	for (int temp_id = 0; temp_id < TEMP_SENSOR_COUNT; temp_id++) {
		if (temperature_filter_is_support(temp_id))
			temperature_filter_reset(temp_id);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, temperature_filter_resume, HOOK_PRIO_DEFAULT);
