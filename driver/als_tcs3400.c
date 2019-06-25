/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMS TCS3400 light sensor driver
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_tcs3400.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"

#define CPRINTS(fmt, args...) cprints(CC_ACCEL, "%s "fmt, __func__, ## args)

#ifdef CONFIG_ACCEL_FIFO
static volatile uint32_t last_interrupt_timestamp;
#endif

static inline int tcs3400_i2c_read8(const struct motion_sensor_t *s,
				    int reg, int *data)
{
	return i2c_read8__7bf(s->port, s->i2c_spi_addr__7bf, reg, data);
}

static inline int tcs3400_i2c_write8(const struct motion_sensor_t *s,
				     int reg, int data)
{
	return i2c_write8__7bf(s->port, s->i2c_spi_addr__7bf, reg, data);
}

static void tcs3400_read_deferred(void)
{
	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ALS_TCS3400_INT_EVENT, 0);
}
DECLARE_DEFERRED(tcs3400_read_deferred);

/* convert ATIME register to integration time, in microseconds */
static int tcs3400_get_integration_time(int atime)
{
	return 2780 * (256 - atime);
}

static int tcs3400_read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret;

	/* Enable power, ADC, and interrupt to start cycle */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_COLLECTING);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT)) {
		int atime;

		ret = tcs3400_i2c_read8(s, TCS_I2C_ATIME, &atime);
		if (ret)
			return ret;

		hook_call_deferred(&tcs3400_read_deferred_data,
				tcs3400_get_integration_time(atime));
	}

	/*
	 * If write succeeded, we've started the read process, but can't
	 * complete it yet until data is ready, so pass back EC_RES_IN_PROGRESS
	 * to inform upper level that read data process is under way and data
	 * will be delivered when available.
	 */
	return EC_RES_IN_PROGRESS;
}

static int tcs3400_rgb_read(const struct motion_sensor_t *s, intv3_t v)
{
	return EC_SUCCESS;
}

static int tcs3400_post_events(struct motion_sensor_t *s, uint32_t last_ts)
{
	/*
	 * Rule says RGB sensor is right after ALS sensor, and this
	 * routine will only get called from ALS sensor driver.
	 */
	struct motion_sensor_t *rgb_s = s + 1;
	struct als_drv_data_t *drv_data = TCS3400_DRV_DATA(s);
	struct tcs3400_rgb_drv_data_t *rgb_drv_data =
			TCS3400_RGB_DRV_DATA(rgb_s);
	struct ec_response_motion_sensor_data vector;
	uint8_t light_data[TCS_RGBC_DATA_SIZE];
	int *v = s->raw_xyz;
	int retries = 20; /* 400 ms max */
	int rgb_data[3];
	int data = 0;
	int i, ret;

	/* Make sure data is valid */
	do {
		ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &data);
		if (ret)
			return ret;
		if (!(data & TCS_I2C_STATUS_RGBC_VALID)) {
			retries--;
			if (retries == 0) {
				CPRINTS("RGBC not valid (0x%x)", data);
				return EC_ERROR_UNCHANGED;
			}
			msleep(20);
		}
	} while (!(data & TCS_I2C_STATUS_RGBC_VALID));

	/* Read the light registers */
	ret = i2c_read_block__7bf(s->port, s->i2c_spi_addr__7bf,
			     TCS_DATA_START_LOCATION,
			     light_data, sizeof(light_data));
	if (ret)
		return ret;

	/* Transfer Clear data into sensor struct and into fifo */
	data = (light_data[1] << 8) | light_data[0];
	data += drv_data->als_cal.offset;
	data = data * drv_data->als_cal.scale +
			data * drv_data->als_cal.uscale / 10000;

	/* Correct negative values to zero */
	if (data < 0) {
		CPRINTS("Negative clear val 0x%x set to 0", data);
		data = 0;
	}

	if (data != drv_data->last_value) {
		drv_data->last_value = data;
		vector.flags = 0;
#ifdef CONFIG_ACCEL_SPOOF_MODE
		if (s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
			for (i = 0; i < 3; i++)
				vector.data[i] = v[i] = s->spoof_xyz[i];
			goto skip_clear_vector_load;
		}
#endif  /* defined(CONFIG_ACCEL_SPOOF_MODE) */

		vector.data[X] = v[X] = data;
		vector.data[Y] = v[Y] = 0;
		vector.data[Z] = v[Z] = 0;

#ifdef CONFIG_ACCEL_SPOOF_MODE
skip_clear_vector_load:
#endif

#ifdef CONFIG_ACCEL_FIFO
		vector.sensor_num = s - motion_sensors;
		motion_sense_fifo_stage_data(&vector, s, 3, last_ts);
#endif
	}

#ifdef CONFIG_ACCEL_SPOOF_MODE
	if (s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
		rgb_data[X] = s->spoof_xyz[X];
		rgb_data[Y] = s->spoof_xyz[Y];
		rgb_data[Z] = s->spoof_xyz[Z];
		goto skip_rgb_load;
	}
#endif

	for (i = 0; i < 3; i++) {
		/* rgb data at indicies 2 thru 7 inclusive in light_data */
		int index = 3 + (i * 2);

		rgb_data[i] = ((light_data[index] << 8) | light_data[index-1]);
		rgb_data[i] += rgb_drv_data->rgb_cal[i].offset;
		rgb_data[i] *= rgb_drv_data->rgb_cal[i].scale >> 15;
		rgb_data[i] = rgb_data[i] * rgb_drv_data->device_scale +
			rgb_data[i] * rgb_drv_data->device_uscale / 10000;

		/* Correct any negative values to zero */
		if (rgb_data[i] < 0) {
			CPRINTS("Negative rgb channel #%d val 0x%x set to 0",
				i, rgb_data[i]);
			rgb_data[i] = 0;
		}
	}

#ifdef CONFIG_ACCEL_SPOOF_MODE
skip_rgb_load:
#endif
	/* If anything changed, transfer RGB data */
	if ((rgb_drv_data->last_value[X] != rgb_data[X]) ||
		(rgb_drv_data->last_value[Y] != rgb_data[Y]) ||
		(rgb_drv_data->last_value[Z] != rgb_data[Z])) {
		for (i = 0; i < 3; i++)
			rgb_drv_data->last_value[i] = rgb_data[i];
		v = rgb_s->raw_xyz;
		vector.flags = 0;
#ifdef CONFIG_ACCEL_SPOOF_MODE
		if (rgb_s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
			for (i = 0; i < 3; i++)
				vector.data[i] = v[i] = rgb_s->spoof_xyz[i];
			goto skip_vector_load;
		}
#endif  /* defined(CONFIG_ACCEL_SPOOF_MODE) */

		vector.data[X] = v[X] = rgb_data[X];
		vector.data[Y] = v[Y] = rgb_data[Y];
		vector.data[Z] = v[Z] = rgb_data[Z];

#ifdef CONFIG_ACCEL_SPOOF_MODE
skip_vector_load:
#endif
		vector.sensor_num = rgb_s - motion_sensors;
		motion_sense_fifo_stage_data(&vector, rgb_s, 3, last_ts);
	}
	motion_sense_fifo_commit_data();
	return EC_SUCCESS;
}

void tcs3400_interrupt(enum gpio_signal signal)
{
#ifdef CONFIG_ACCEL_FIFO
	last_interrupt_timestamp = __hw_clock_source_read();
#endif
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ALS_TCS3400_INT_EVENT, 0);
}

/*
 * tcs3400_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt,
 * and posts those events via motion_sense_fifo_stage_data()..
 */
static int tcs3400_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int status = 0;
	int ret = EC_SUCCESS;

	if (!(*event & CONFIG_ALS_TCS3400_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
	if (ret)
		return ret;

	/* Disable future interrupts */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_IDLE);
	if (ret)
		return ret;

	if ((status & TCS_I2C_STATUS_RGBC_VALID) ||
			((status & TCS_I2C_STATUS_ALS_IRQ) &&
			(status & TCS_I2C_STATUS_ALS_VALID)) ||
			IS_ENABLED(CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT)) {
		ret = tcs3400_post_events(s, last_interrupt_timestamp);
		if (ret)
			return ret;
	}

	tcs3400_i2c_write8(s, TCS_I2C_AICLEAR, 0);

	/* Disable ADC and turn off internal oscillator */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_SUSPEND);
	if (ret)
		return ret;

	return ret;
}

static int tcs3400_rgb_get_range(const struct motion_sensor_t *s)
{
	/* Currently, calibration info is same for all channels */
	return (TCS3400_RGB_DRV_DATA(s)->device_scale << 16) |
			TCS3400_RGB_DRV_DATA(s)->device_uscale;
}

static int tcs3400_rgb_set_range(const struct motion_sensor_t *s,
				 int range,
				 int rnd)
{
	TCS3400_RGB_DRV_DATA(s)->device_scale = range >> 16;
	TCS3400_RGB_DRV_DATA(s)->device_uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_scale(const struct motion_sensor_t *s,
				 uint16_t *scale,
				 int16_t *temp)
{
	scale[X] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].scale;
	scale[Y] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].scale;
	scale[Z] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].scale;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_scale(const struct motion_sensor_t *s,
				 const uint16_t *scale,
				 int16_t temp)
{
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].scale = scale[X];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].scale = scale[Y];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].scale = scale[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_offset(const struct motion_sensor_t *s,
				  int16_t *offset,
				  int16_t *temp)
{
	offset[X] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].offset;
	offset[Y] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].offset;
	offset[Z] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].offset;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_offset(const struct motion_sensor_t *s,
				  const int16_t *offset,
				  int16_t temp)
{
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].offset = offset[X];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].offset = offset[Y];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].offset = offset[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_RGB_DRV_DATA(s)->rate;
}

static int tcs3400_rgb_set_data_rate(const struct motion_sensor_t *s,
				     int rate,
				     int rnd)
{
	TCS3400_RGB_DRV_DATA(s)->rate = rate;
	return EC_SUCCESS;
}

static int tcs3400_get_range(const struct motion_sensor_t *s)
{
	return (TCS3400_DRV_DATA(s)->als_cal.scale << 16) |
			(TCS3400_DRV_DATA(s)->als_cal.uscale);
}

static int tcs3400_set_range(const struct motion_sensor_t *s,
			     int range,
			     int rnd)
{
	TCS3400_DRV_DATA(s)->als_cal.scale = range >> 16;
	TCS3400_DRV_DATA(s)->als_cal.uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int tcs3400_get_offset(const struct motion_sensor_t *s,
			      int16_t *offset,
			      int16_t *temp)
{
	offset[X] = TCS3400_DRV_DATA(s)->als_cal.offset;
	offset[Y] = 0;
	offset[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_set_offset(const struct motion_sensor_t *s,
			      const int16_t *offset,
			      int16_t temp)
{
	TCS3400_DRV_DATA(s)->als_cal.offset = offset[X];
	return EC_SUCCESS;
}

static int tcs3400_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_DRV_DATA(s)->rate;
}

static int tcs3400_set_data_rate(const struct motion_sensor_t *s,
				 int rate,
				 int rnd)
{
	enum tcs3400_mode mode;
	int data;
	int ret;

	if (rate == 0) {
		/* Suspend driver */
		mode = TCS3400_MODE_SUSPEND;
	} else {
		/*
		 * We set the sensor for continuous mode,
		 * integrating over 800ms.
		 * Do not allow range higher than 1Hz.
		 */
		if (rate > 1000)
			rate = 1000;
		mode = TCS3400_MODE_COLLECTING;
	}
	TCS3400_DRV_DATA(s)->rate = rate;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ENABLE, &data);
	if (ret)
		return ret;

	data = (data & TCS_I2C_ENABLE_MASK) | mode;
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, data);

	return ret;
}

/**
 * Initialise TCS3400 light sensor.
 */
static int tcs3400_rgb_init(const struct motion_sensor_t *s)
{
	return sensor_init_done(s);
}

static int tcs3400_init(const struct motion_sensor_t *s)
{
	/*
	 * These are default power-on register values with two exceptions:
	 * Set ATIME = 0 (712 ms)
	 * Set AGAIN = 16 (0x10)  (AGAIN is in CONTROL register)
	 */
	const struct reg_data {
		uint8_t reg;
		uint8_t data;
	} defaults[] = {
		{ TCS_I2C_ENABLE, 0 },
		{ TCS_I2C_ATIME, TCS_DEFAULT_ATIME },
		{ TCS_I2C_WTIME, 0xFF },
		{ TCS_I2C_AILTL, 0 },
		{ TCS_I2C_AILTH, 0 },
		{ TCS_I2C_AIHTL, 0 },
		{ TCS_I2C_AIHTH, 0 },
		{ TCS_I2C_PERS, 0 },
		{ TCS_I2C_CONFIG, 0x40 },
		{ TCS_I2C_CONTROL, (TCS_DEFAULT_AGAIN & TCS_I2C_CONTROL_MASK)},
		{ TCS_I2C_AUX, 0 },
		{ TCS_I2C_IR, 0 },
		{ TCS_I2C_CICLEAR, 0 },
		{ TCS_I2C_AICLEAR, 0 }
	};
	int data = 0;
	int ret;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ID, &data);
	if (ret) {
		CPRINTS("failed reading ID reg 0x%x, ret=%d", TCS_I2C_ID, ret);
		return ret;
	}
	if ((data != TCS340015_DEVICE_ID) && (data != TCS340037_DEVICE_ID)) {
		CPRINTS("no ID match, data = 0x%x", data);
		return EC_ERROR_ACCESS_DENIED;
	}

	/* reset chip to default power-on settings, changes ATIME and CONTROL */
	for (int x = 0; x < ARRAY_SIZE(defaults); x++) {
		ret = tcs3400_i2c_write8(s, defaults[x].reg, defaults[x].data);
		if (ret)
			return ret;
	}

	return sensor_init_done(s);
}

const struct accelgyro_drv tcs3400_drv = {
	.init = tcs3400_init,
	.read = tcs3400_read,
	.set_range = tcs3400_set_range,
	.get_range = tcs3400_get_range,
	.set_offset = tcs3400_set_offset,
	.get_offset = tcs3400_get_offset,
	.set_data_rate = tcs3400_set_data_rate,
	.get_data_rate = tcs3400_get_data_rate,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = tcs3400_irq_handler,
#endif
};

const struct accelgyro_drv tcs3400_rgb_drv = {
	.init = tcs3400_rgb_init,
	.read = tcs3400_rgb_read,
	.set_range = tcs3400_rgb_set_range,
	.get_range = tcs3400_rgb_get_range,
	.set_offset = tcs3400_rgb_set_offset,
	.get_offset = tcs3400_rgb_get_offset,
	.set_scale = tcs3400_rgb_set_scale,
	.get_scale = tcs3400_rgb_get_scale,
	.set_data_rate = tcs3400_rgb_set_data_rate,
	.get_data_rate = tcs3400_rgb_get_data_rate,
};
