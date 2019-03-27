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
	return i2c_read8(s->port, s->addr, reg, data);
}

static inline int tcs3400_i2c_write8(const struct motion_sensor_t *s,
				     int reg, int data)
{
	return i2c_write8(s->port, s->addr, reg, data);
}

static int tcs3400_read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret;
	int data;

	/* Enable the ADC to start cycle */
	ret = tcs3400_i2c_read8(s, TCS_I2C_ENABLE, &data);
	if (ret)
		return ret;

	/* mask value to assure writing 0 to reserved bits */
	data = (data & ~TCS_I2C_ENABLE_MASK) | TCS3400_MODE_COLLECTING;
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, data);

	/*
	 * If write succeeded, we've started the read process, but can't
	 * complete it yet until data is ready, so pass back EC_RES_IN_PROGRESS
	 * to inform upper level that read data process is under way and data
	 * will be delivered when available.
	 */
	if (ret == EC_SUCCESS)
		ret = EC_RES_IN_PROGRESS;

	return ret;
}

static int tcs3400_post_events(struct motion_sensor_t *s, uint32_t last_ts)
{
	struct tcs3400_drv_data_t *drv_data = TCS3400_DRV_DATA(s);
	struct ec_response_motion_sensor_data vector;
	uint8_t light_data[TCS_RGBC_DATA_SIZE];
	int *v = s->raw_xyz;
	int retries = 20; /* 400 ms max */
	int data = 0;
	int ret;

	/* Make sure data is valid */
	do {
		ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &data);
		if (ret)
			return ret;
		if (!(data & TCS_I2C_STATUS_RGBC_VALID)) {
			retries--;
			if (retries == 0)
				return EC_ERROR_UNCHANGED;
			CPRINTS("RGBC not valid (0x%x)", data);
			msleep(20);
		}
	} while (!(data & TCS_I2C_STATUS_RGBC_VALID));

	/* Read the light registers */
	ret = i2c_read_block(s->port, s->addr, TCS_DATA_START_LOCATION,
			light_data, sizeof(light_data));
	if (ret)
		return ret;

	/* Transfer Clear data into sensor struct and into fifo */
	data = (light_data[1] << 8) | light_data[0];
	data += drv_data->als_cal.offset;
	data = data * drv_data->als_cal.scale +
			data * drv_data->als_cal.uscale / 10000;

	if (data < 0) {
		CPRINTS("Negative clear val 0x%x set to 0", data);
		data = 0;
	}

	if (data != drv_data->last_value) {
		drv_data->last_value = data;
		vector.flags = 0;
#ifdef CONFIG_ACCEL_SPOOF_MODE
		if (s->in_spoof_mode) {
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
		motion_sense_fifo_add_data(&vector, s, 3, last_ts);
#endif
	}

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

/**
 * tcs3400_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt,
 * and posts those events via motion_sense_fifo_add_data()..
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
	ret = tcs3400_i2c_read8(s, TCS_I2C_ENABLE, &status);
	if (ret)
		return ret;

	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE,
			(status & ~TCS_I2C_ENABLE_INT_ENABLE));

	if ((status & TCS_I2C_STATUS_RGBC_VALID) ||
		((status & TCS_I2C_STATUS_ALS_IRQ) &&
		(status & TCS_I2C_STATUS_ALS_VALID))) {

		ret = tcs3400_post_events(s, last_interrupt_timestamp);
		if (ret)
			return ret;
	}

	ret = tcs3400_i2c_write8(s, TCS_I2C_AICLEAR, 0);

	return ret;
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
static int tcs3400_init(const struct motion_sensor_t *s)
{
	/*
	 * These are default power-on register values with two exceptions:
	 * Set ATIME = 0x4 (700.88ms)
	 * Set AGAIN = 16 (0x10)  (AGAIN is in CONTROL register)
	 */
	const struct reg_data {
		uint8_t reg;
		uint8_t data;
	} defaults[] = { { TCS_I2C_ENABLE, 0 },
			 { TCS_I2C_ATIME, 0x4 },
			 { TCS_I2C_WTIME, 0xFF },
			 { TCS_I2C_AILTL, 0 },
			 { TCS_I2C_AILTH, 0 },
			 { TCS_I2C_AIHTL, 0 },
			 { TCS_I2C_AIHTH, 0 },
			 { TCS_I2C_PERS, 0 },
			 { TCS_I2C_CONFIG, 0x40 },
			 { TCS_I2C_CONTROL, 0x10 },
			 { TCS_I2C_AUX, 0 },
			 { TCS_I2C_IR, 0 },
			 { TCS_I2C_CICLEAR, 0 },
			 { TCS_I2C_AICLEAR, 0 } };
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
