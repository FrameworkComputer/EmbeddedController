/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "atomic.h"
#include "hwtimer.h"
#include "online_calibration.h"
#include "common.h"
#include "mag_cal.h"
#include "util.h"
#include "vec3.h"
#include "task.h"
#include "ec_commands.h"
#include "accel_cal.h"
#include "mkbp_event.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)

#ifndef CONFIG_MKBP_EVENT
#error "Must use CONFIG_MKBP_EVENT for online calibration"
#endif /* CONFIG_MKBP_EVENT */

/** Bitmap telling which online calibration values are valid. */
static uint32_t sensor_calib_cache_valid_map;
/** Bitmap telling which online calibration values are dirty. */
static uint32_t sensor_calib_cache_dirty_map;

struct mutex g_calib_cache_mutex;

static int get_temperature(struct motion_sensor_t *sensor, int *temp)
{
	struct online_calib_data *entry = sensor->online_calib_data;
	uint32_t now;

	if (sensor->drv->read_temp == NULL)
		return EC_ERROR_UNIMPLEMENTED;

	now = __hw_clock_source_read();
	if (entry->last_temperature < 0 ||
	    time_until(entry->last_temperature_timestamp, now) >
	    CONFIG_TEMP_CACHE_STALE_THRES) {
		int t;
		int rc = sensor->drv->read_temp(sensor, &t);

		if (rc == EC_SUCCESS) {
			entry->last_temperature = t;
			entry->last_temperature_timestamp = now;
		} else {
			return rc;
		}
	}

	*temp = entry->last_temperature;
	return EC_SUCCESS;
}

static void data_int16_to_fp(const struct motion_sensor_t *s,
			     const int16_t *data, fpv3_t out)
{
	int i;
	fp_t range = INT_TO_FP(s->drv->get_range(s));

	for (i = 0; i < 3; ++i) {
		fp_t v = INT_TO_FP((int32_t)data[i]);

		out[i] = fp_div(v, INT_TO_FP((data[i] >= 0) ? 0x7fff : 0x8000));
		out[i] = fp_mul(out[i], range);
		/* Check for overflow */
		out[i] = CLAMP(out[i], -range, range);
	}
}

static void data_fp_to_int16(const struct motion_sensor_t *s,
			     const fpv3_t data, int16_t *out)
{
	int i;
	fp_t range = INT_TO_FP(s->drv->get_range(s));

	for (i = 0; i < 3; ++i) {
		int32_t iv;
		fp_t v = fp_div(data[i], range);

		v = fp_mul(v, INT_TO_FP(
			(data[i] >= INT_TO_FP(0)) ? 0x7fff : 0x8000));
		iv = FP_TO_INT(v);
		/* Check for overflow */
		out[i] = CLAMP(iv, (int32_t)0xffff8000, (int32_t)0x00007fff);
	}
}

void online_calibration_init(void)
{
	size_t i;

	for (i = 0; i < SENSOR_COUNT; i++) {
		struct motion_sensor_t *s = motion_sensors + i;
		void *type_specific_data = NULL;

		if (s->online_calib_data) {
			s->online_calib_data->last_temperature = -1;
			type_specific_data =
				s->online_calib_data->type_specific_data;
		}

		if (!type_specific_data)
			continue;

		switch (s->type) {
		case MOTIONSENSE_TYPE_ACCEL: {
			accel_cal_reset((struct accel_cal *)type_specific_data);
			break;
		}
		case MOTIONSENSE_TYPE_MAG: {
			init_mag_cal((struct mag_cal_t *)type_specific_data);
			break;
		}
		default:
			break;
		}
	}
}

bool online_calibration_has_new_values(void)
{
	bool has_dirty;

	mutex_lock(&g_calib_cache_mutex);
	has_dirty = sensor_calib_cache_dirty_map != 0;
	mutex_unlock(&g_calib_cache_mutex);

	return has_dirty;
}

bool online_calibration_read(int sensor_num, int16_t out[3])
{
	bool has_valid;

	mutex_lock(&g_calib_cache_mutex);
	has_valid = (sensor_calib_cache_valid_map & BIT(sensor_num)) != 0;
	if (has_valid) {
		/* Update data in out */
		memcpy(out,
		       motion_sensors[sensor_num].online_calib_data->cache,
		       sizeof(out));
		/* Clear dirty bit */
		sensor_calib_cache_dirty_map &= ~(1 << sensor_num);
	}
	mutex_unlock(&g_calib_cache_mutex);

	return has_valid;
}

int online_calibration_process_data(
	struct ec_response_motion_sensor_data *data,
	struct motion_sensor_t *sensor,
	uint32_t timestamp)
{
	size_t sensor_num = motion_sensors - sensor;
	int rc;
	int temperature;
	struct online_calib_data *calib_data;

	calib_data = sensor->online_calib_data;
	switch (sensor->type) {
	case MOTIONSENSE_TYPE_ACCEL: {
		struct accel_cal *cal =
			(struct accel_cal *)(calib_data->type_specific_data);
		fpv3_t fdata;

		/* Temperature is required for accelerometer calibration. */
		rc = get_temperature(sensor, &temperature);
		if (rc != EC_SUCCESS)
			return rc;

		data_int16_to_fp(sensor, data->data, fdata);
		if (accel_cal_accumulate(cal, timestamp, fdata[X], fdata[Y],
					 fdata[Z], temperature)) {
			mutex_lock(&g_calib_cache_mutex);
			/* Convert result to the right scale. */
			data_fp_to_int16(sensor, cal->bias, calib_data->cache);
			/* Set valid and dirty. */
			sensor_calib_cache_valid_map |=	BIT(sensor_num);
			sensor_calib_cache_dirty_map |=	BIT(sensor_num);
			mutex_unlock(&g_calib_cache_mutex);
			/* Notify the AP. */
			mkbp_send_event(EC_MKBP_EVENT_ONLINE_CALIBRATION);
		}
		break;
	}
	case MOTIONSENSE_TYPE_MAG: {
		struct mag_cal_t *cal =
			(struct mag_cal_t *) (calib_data->type_specific_data);
		int idata[] = {
			(int)data->data[X],
			(int)data->data[Y],
			(int)data->data[Z],
		};

		if (mag_cal_update(cal, idata)) {
			mutex_lock(&g_calib_cache_mutex);
			/* Copy the values */
			calib_data->cache[X] = cal->bias[X];
			calib_data->cache[Y] = cal->bias[Y];
			calib_data->cache[Z] = cal->bias[Z];
			/* Set valid and dirty. */
			sensor_calib_cache_valid_map |= BIT(sensor_num);
			sensor_calib_cache_dirty_map |= BIT(sensor_num);
			mutex_unlock(&g_calib_cache_mutex);
			/* Notify the AP. */
			mkbp_send_event(EC_MKBP_EVENT_ONLINE_CALIBRATION);
		}
		break;
	}
	default:
		break;
	}

	return EC_SUCCESS;
}

