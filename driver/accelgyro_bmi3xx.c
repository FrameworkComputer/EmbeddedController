/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI3XX accelerometer and gyroscope module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "accelgyro_bmi323.h"
#include "accelgyro_bmi_common.h"
#include "console.h"
#include "hwtimer.h"
#include "i2c.h"
#include "init_rom.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

/* Sensor definition */
STATIC_IF(CONFIG_BMI_ORIENTATION_SENSOR) void irq_set_orientation(
						struct motion_sensor_t *s);

STATIC_IF(CONFIG_ACCEL_FIFO) volatile uint32_t last_interrupt_timestamp;

static uint8_t bmi3_buffer[BMI3_FIFO_BUFFER];

static inline int bmi3_read_n(const struct motion_sensor_t *s, const int reg,
			      uint8_t *data_ptr, const int len)
{
	return bmi_read_n(s->port, s->i2c_spi_addr_flags, reg, data_ptr, len);
}

static inline int bmi3_write_n(const struct motion_sensor_t *s, const int reg,
			       uint8_t *data_ptr, const int len)
{
	return bmi_write_n(s->port, s->i2c_spi_addr_flags, reg, data_ptr, len);
}

#ifdef CONFIG_ACCEL_INTERRUPTS

#ifdef CONFIG_BMI_ORIENTATION_SENSOR

static void irq_set_orientation(struct motion_sensor_t *s)
{
	int ret;
	uint8_t reg_data[4];
	uint8_t orient_data;

	enum motionsensor_orientation orientation =
					MOTIONSENSE_ORIENTATION_UNKNOWN;

	RETURN_ERROR(bmi3_read_n(s, BMI3_FEATURE_EVENT_EXT, reg_data, 4));

	orient_data = reg_data[2] & BMI3_PORTRAIT_LANDSCAPE_MASK;

	if (BMI_GET_DATA(s)->raw_orientation != orient_data) {
		BMI_GET_DATA(s)->raw_orientation = orient_data;

		switch (orient_data) {
		case BMI3_ORIENT_PORTRAIT:
			orientation = MOTIONSENSE_ORIENTATION_PORTRAIT;
			break;
		case BMI3_PORTRAIT_INVERT:
			orientation =
				MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_PORTRAIT;
			break;
		case BMI3_LANDSCAPE:
			orientation = MOTIONSENSE_ORIENTATION_LANDSCAPE;
			break;
		case BMI3_LANDSCAPE_INVERT:
			orientation =
				MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_LANDSCAPE;
			break;
		default:
			break;
		}

		orientation = motion_orientation_remap(s, orientation);
		*motion_orientation_ptr(s) = orientation;
	}
}

#endif  /* CONFIG_BMI_ORIENTATION_SENSOR */

/*
 * bmi3xx_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->irq_handler().
 */
void bmi3xx_interrupt(enum gpio_signal signal)
{
	if (IS_ENABLED(CONFIG_ACCEL_FIFO))
		last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCELGYRO_BMI3XX_INT_EVENT);
}

static int enable_fifo(const struct motion_sensor_t *s, int enable)
{
	/* Set FIFO config to enable accel gyro data */
	uint8_t reg_data[2] = {0, 0};
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);

	reg_data[0] = 0;

	if (enable) {
		reg_data[1] = BMI3_FIFO_ACC_EN | BMI3_FIFO_GYR_EN;
		data->flags |= 1 << (s->type + BMI_FIFO_FLAG_OFFSET);
	} else {
		data->flags &= ~(1 << (s->type + BMI_FIFO_FLAG_OFFSET));
	}

	return bmi3_write_n(s, BMI3_REG_FIFO_CONF, reg_data, 2);
}

static int config_interrupt(const struct motion_sensor_t *s)
{
	int ret;
	uint8_t reg_data[6] = {0};

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	mutex_lock(s->mutex);

	/* Clear the FIFO using Flush command */
	reg_data[0] = BMI3_ENABLE;
	reg_data[1] = 0;
	ret = bmi3_write_n(s, BMI3_REG_FIFO_CTRL, reg_data, 2);
	if (ret)
		goto err_unlock;

	/* Map FIFO water-mark and FIFO full to INT1 pin */
	ret = bmi3_read_n(s, BMI3_REG_INT_MAP1, reg_data, 6);
	if (ret)
		goto err_unlock;

	reg_data[5] = BMI3_SET_BITS(reg_data[5], BMI3_FWM_INT, BMI3_INT1);
	reg_data[5] = BMI3_SET_BITS(reg_data[5], BMI3_FFULL_INT, BMI3_INT1);
	if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR)) {
		/* Map orientation to INT1 pin */
		reg_data[2] = BMI3_SET_BITS(reg_data[2], BMI3_ORIENT_INT,
						BMI3_INT1);
	}

	ret = bmi3_write_n(s, BMI3_REG_INT_MAP1, &reg_data[2], 4);
	if (ret)
		goto err_unlock;

	/* Set FIFO water-mark to read data whenever available */
	reg_data[0] = 1;
	reg_data[1] = 0;

	ret = bmi3_write_n(s, BMI3_REG_FIFO_WATERMARK, reg_data, 2);
	if (ret)
		goto err_unlock;

	/* Get the previous configuration data */
	ret = bmi3_read_n(s, BMI3_REG_IO_INT_CTRL, reg_data, 4);
	if (ret)
		goto err_unlock;

	reg_data[2] = BMI3_SET_BIT_POS0(reg_data[2], BMI3_INT1_LVL,
					BMI3_INT_ACTIVE_LOW);

	reg_data[2] = BMI3_SET_BITS(reg_data[2], BMI3_INT1_OD,
				    BMI3_INT_PUSH_PULL);

	reg_data[2] = BMI3_SET_BITS(reg_data[2], BMI3_INT1_OUTPUT_EN,
				    BMI3_INT_OUTPUT_ENABLE);

	/*
	 * Set the interrupt pin configurations
	 */
	ret = bmi3_write_n(s, BMI3_REG_IO_INT_CTRL, &reg_data[2], 2);
	if (ret)
		goto err_unlock;

	if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR)) {
		/* Enable the orientation feature in BMI3 */
		ret = bmi3_read_n(s, BMI3_FEATURE_IO_0, reg_data, 4);
		if (ret)
			goto err_unlock;

		reg_data[2] |= BMI3_ANY_MOTION_X_EN_MASK;
		ret = bmi3_write_n(s, BMI3_FEATURE_IO_0, &reg_data[2], 2);
		if (ret)
			goto err_unlock;

		/* Write to feature engine */
		reg_data[0] = 1;
		reg_data[1] = 0;
		ret = bmi3_write_n(s, BMI3_FEATURE_IO_STATUS, reg_data, 2);
	}

err_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

int bmi3_parse_fifo_data(struct motion_sensor_t *s, struct bmi3_fifo_frame
			 *fifo_frame, uint32_t last_ts)
{
	/* Start index for FIFO parsing after I2C sync byte removal */
	size_t fifo_index = 2;

	/* Variable to store LSB and MSB value */
	uint16_t data_lsb, data_msb;

	/* Variable to store I2C sync data which will get in FIFO data */
	uint16_t i2c_sync_data;

	uint16_t fifo_size = 0;

	struct ec_response_motion_sensor_data vect;

	bool observed[2];

	struct bmi3_fifo_data raw_data[NUM_OF_PRIMARY_SENSOR];

	uint8_t sens_cnt = 0, reg_data[2];

	struct bmi_drv_data_t *data = BMI_GET_DATA(s);

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	if (!(data->flags & (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET))) {

		/*
		 * The FIFO was disabled while we were processing it
		 * Flush potential left over:
		 * When sensor is resumed, we won't read old data.
		 */

		/* Clear the FIFO using Flush command */
		reg_data[0] = BMI3_ENABLE;
		reg_data[1] = 0;
		return bmi3_write_n(s, BMI3_REG_FIFO_CTRL, reg_data, 2);
	}

	/* Parse the length of data read excluding I2C sync bytes */
	fifo_size = fifo_frame->available_fifo_len - 2;

	while (fifo_size > 0) {
		observed[SENSOR_ACCEL] = false;
		observed[SENSOR_GYRO] = false;

		/**
		 * If we are reading some constant 64 bytes every time
		 * then 0x80 may even come after
		 * SENSOR ACCEL IS ENABLED
		 */
		if ((fifo_frame->available_fifo_sens & BMI3_FIFO_ACC_EN)
		    && (fifo_size != 0)) {
			/* In-case of FIFO read fail it has only 0x8000 */
			if (fifo_size >= 2) {
				if (bmi3_buffer[fifo_index] == 0x00
				    && bmi3_buffer[fifo_index+1] == 0x80) {
					break;
				}
			} else {
				observed[SENSOR_ACCEL] = false;
				observed[SENSOR_GYRO] = false;
				fifo_size = 0;
			}

			if (fifo_size >= BMI3_LENGTH_FIFO_ACC) {
				/* Accelerometer raw x data */
				data_lsb = bmi3_buffer[fifo_index++];
				data_msb = bmi3_buffer[fifo_index++];

				/* To store the I2C sync data */
				i2c_sync_data = (uint16_t)((data_msb << 8)
						| data_lsb);

				if (i2c_sync_data
				     != BMI3_FIFO_ACCEL_I2C_SYNC_FRAME) {
					observed[SENSOR_ACCEL] = true;
					raw_data[SENSOR_ACCEL].x =
						(int16_t)((data_msb << 8)
						| data_lsb);
					/* Accelerometer raw y data */
					data_lsb = bmi3_buffer[fifo_index++];
					data_msb = bmi3_buffer[fifo_index++];
					raw_data[SENSOR_ACCEL].y =
						(int16_t)((data_msb << 8)
						| data_lsb);

					/* Accelerometer raw z data */
					data_lsb = bmi3_buffer[fifo_index++];
					data_msb = bmi3_buffer[fifo_index++];
					raw_data[SENSOR_ACCEL].z =
						(int16_t)((data_msb << 8)
						| data_lsb);
				} else {
					fifo_index = fifo_index + 4;
					observed[SENSOR_ACCEL] = false;
				}

				fifo_size -= BMI3_LENGTH_FIFO_ACC;
			} else {
				observed[SENSOR_ACCEL] = false;
				observed[SENSOR_GYRO] = false;
				fifo_size = 0;
			}
		}

		if ((fifo_frame->available_fifo_sens & BMI3_FIFO_GYR_EN)
		    && (fifo_size != 0)) {

			/* In-case of FIFO read fail it has only 0x8000 */
			if (fifo_size >= 2) {
				if (bmi3_buffer[fifo_index] == 0x00 &&
				    bmi3_buffer[fifo_index+1] == 0x80) {
					break;
				}
			} else {
				observed[SENSOR_ACCEL] = false;
				observed[SENSOR_GYRO] = false;
				fifo_size = 0;
			}

			if (fifo_size >= BMI3_LENGTH_FIFO_GYR) {

				data_lsb = bmi3_buffer[fifo_index++];
				data_msb = bmi3_buffer[fifo_index++];

				/* To store the I2C sync data */
				i2c_sync_data = (uint16_t)((data_msb << 8)
						| data_lsb);
				if (i2c_sync_data
				    != BMI3_FIFO_GYRO_I2C_SYNC_FRAME) {

					observed[SENSOR_GYRO] = true;

					raw_data[SENSOR_GYRO].x =
						(int16_t)((data_msb << 8)
						| data_lsb);

					/* Accelerometer raw y data */
					data_lsb = bmi3_buffer[fifo_index++];
					data_msb = bmi3_buffer[fifo_index++];
					raw_data[SENSOR_GYRO].y =
						(int16_t)((data_msb << 8)
						| data_lsb);

					/* Accelerometer raw z data */
					data_lsb = bmi3_buffer[fifo_index++];
					data_msb = bmi3_buffer[fifo_index++];
					raw_data[SENSOR_GYRO].z =
						(int16_t)((data_msb << 8) |
						data_lsb);
				} else {
					fifo_index = fifo_index + 4;
					observed[SENSOR_GYRO] = false;
				}

				fifo_size -= BMI3_LENGTH_FIFO_GYR;
			} else {
				observed[SENSOR_ACCEL] = false;
				observed[SENSOR_GYRO] = false;
				fifo_size = 0;
			}
		}

		for (sens_cnt = 0; sens_cnt < NUM_OF_PRIMARY_SENSOR;
		     sens_cnt++) {

			if (observed[sens_cnt]) {

				struct motion_sensor_t *sens_output = s +
								sens_cnt;

				/* TODO:NORMALISE */

				vect.data[X] = raw_data[sens_cnt].x;
				vect.data[Y] = raw_data[sens_cnt].y;
				vect.data[Z] = raw_data[sens_cnt].z;

				vect.flags = 0;

				/* TODO:check this s-motion_sensors */
				vect.sensor_num = sens_cnt;

				motion_sense_fifo_stage_data(&vect,
						sens_output, 3, last_ts);
			}
		}
	}

	return EC_SUCCESS;
}

/*
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
static int irq_handler(struct motion_sensor_t *s,
		uint32_t *event)
{
	int8_t has_read_fifo = 0;
	int ret = 0;
	uint8_t reg_data[4];
	uint16_t int_status;
	uint16_t fifo_fill_level;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL)
	    || (!(*event & CONFIG_ACCELGYRO_BMI3XX_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	/* Get the interrupt status */
	ret = bmi3_read_n(s, BMI3_REG_INT_STATUS_INT1, reg_data, 4);
	int_status = (uint16_t) reg_data[2] | ((uint16_t) reg_data[3] << 8);

	if ((ret == EC_SUCCESS) && ((int_status & BMI3_INT_STATUS_FWM) ||
					(int_status & BMI3_INT_STATUS_FFULL))) {

		struct bmi3_fifo_frame fifo_frame;

		fifo_frame.data = bmi3_buffer;
		fifo_frame.length = BMI3_FIFO_BUFFER;

		/* Get the FIFO frame configurations */
		ret = bmi3_read_n(s, BMI3_REG_FIFO_CONF, reg_data, 4);
		fifo_frame.available_fifo_sens = reg_data[3] & BMI3_FIFO_ALL_EN;

		/* Get the FIFO fill level in words */
		ret = bmi3_read_n(s, BMI3_REG_FIFO_FILL_LVL, reg_data, 4);

		reg_data[3] = BMI3_GET_BIT_POS0(reg_data[3],
						BMI3_FIFO_FILL_LVL);

		fifo_fill_level = ((uint16_t)reg_data[3] << 8 | reg_data[2]);

		/*
		 * fifo_fill_level is in word count so (x2) also we add 2 more
		 * bytes for I2C sync transaction
		 */
		fifo_frame.available_fifo_len = (fifo_fill_level * 2) + 2;

		/* Read FIFO data */
		ret = bmi3_read_n(s, BMI3_REG_FIFO_DATA, bmi3_buffer,
					fifo_fill_level);

		bmi3_parse_fifo_data(s, &fifo_frame, last_interrupt_timestamp);
		has_read_fifo = 1;

		if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR))
			if (BMI3_INT_STATUS_ORIENTATION & int_status)
				irq_set_orientation(s);
	}

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && has_read_fifo)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif /* CONFIG_ACCEL_INTERRUPTS */

static int read_temp(const struct motion_sensor_t *s, int *temp_ptr)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int get_gyro_offset(const struct motion_sensor_t *s, intv3_t v)
{
	int i;
	uint8_t reg_data[14] = { 0 };

	/* Get the accel offset values */
	RETURN_ERROR(bmi3_read_n(s, GYR_DP_OFF_X, reg_data, 14));

	v[0] = ((uint16_t)(reg_data[3] << 8) | reg_data[2]) & 0x03FF;
	v[1] = ((uint16_t)(reg_data[7] << 8) | reg_data[6]) & 0x03FF;
	v[2] = ((uint16_t)(reg_data[11] << 8) | reg_data[10]) & 0x03FF;

	for (i = X; i <= Z; ++i) {
		if (v[i] > 0x01FF)
			v[i] = -1024 + v[i];

		v[i] = round_divide((int64_t)v[i] * BMI_OFFSET_GYRO_MULTI_MDS,
				    BMI_OFFSET_GYRO_DIV_MDS);
	}

	return EC_SUCCESS;
}

int set_gyro_offset(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t reg_data[6] = { 0 };
	uint8_t base_addr[2] = { BMI3_GYRO_OFFSET_ADDR, 0 };
	int i, val[3];

	for (i = X; i <= Z; ++i) {
		val[i] = round_divide((int64_t)v[i] * BMI_OFFSET_GYRO_DIV_MDS,
					BMI_OFFSET_GYRO_MULTI_MDS);
		if (val[i] > 511)
			val[i] = 511;
		if (val[i] < -512)
			val[i] = -512;
		if (val[i] < 0)
			val[i] = 1024 + val[i];
	}

	/*
	 * Set the user accel offset base address to feature engine
	 * transmission address to start DMA transaction
	 */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX, base_addr, 2));

	reg_data[0] = (uint8_t)(val[0] & BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((val[0] & 0x0300) >> 8);
	reg_data[2] = (uint8_t)(val[1] & BMI3_SET_LOW_BYTE);
	reg_data[3] = (uint8_t)((val[1] & 0x0300) >> 8);
	reg_data[4] = (uint8_t)(val[2] & BMI3_SET_LOW_BYTE);
	reg_data[5] = (uint8_t)((val[2] & 0x0300) >> 8);

	/* Set the configuration to the feature engine register */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX_DATA, reg_data,
			6));

	/* Update the offset to the sensor engine */
	reg_data[0] = (uint8_t)(BMI3_CMD_USR_GAIN_OFFS_UPDATE &
				 BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((BMI3_CMD_USR_GAIN_OFFS_UPDATE &
				BMI3_SET_HIGH_BYTE) >> 8);
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_CMD, reg_data, 2));

	return EC_SUCCESS;
}

int get_accel_offset(const struct motion_sensor_t *s, intv3_t v)
{
	int i;
	uint8_t reg_data[14] = { 0 };

	/* Get the accel offset values from user registers */
	RETURN_ERROR(bmi3_read_n(s, ACC_DP_OFF_X, reg_data, 14));

	v[0] = ((uint16_t)(reg_data[3] << 8) | reg_data[2]) & 0x1FFF;
	v[1] = ((uint16_t)(reg_data[7] << 8) | reg_data[6]) & 0x1FFF;
	v[2] = ((uint16_t)(reg_data[11] << 8) | reg_data[10]) & 0x1FFF;

	for (i = X; i <= Z; ++i) {
		if (v[i] > 0x0FFF)
			v[i] = -8192 + v[i];

		v[i] = round_divide((int64_t)v[i] * BMI3_OFFSET_ACC_MULTI_MG,
				    BMI_OFFSET_ACC_DIV_MG);
	}

	return EC_SUCCESS;
}

int set_accel_offset(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t reg_data[6] = { 0 };
	uint8_t base_addr[2] = { BMI3_ACC_OFFSET_ADDR, 0 };
	int i, val[3];

	for (i = X; i <= Z; ++i) {
		val[i] = round_divide((int64_t)v[i] * BMI_OFFSET_ACC_DIV_MG,
				      BMI3_OFFSET_ACC_MULTI_MG);
		if (val[i] > 4095)
			val[i] = 4095;
		if (val[i] < -4096)
			val[i] = -4096;
		if (val[i] < 0)
			val[i] += 8192;
	}

	/*
	 * Set the user accel offset base address to feature engine
	 * transmission address to start DMA transaction
	 */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX, base_addr, 2));

	reg_data[0] = (uint8_t)(val[0] & BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((val[0] & 0x1F00) >> 8);
	reg_data[2] = (uint8_t)(val[1] & BMI3_SET_LOW_BYTE);
	reg_data[3] = (uint8_t)((val[1] & 0x1F00) >> 8);
	reg_data[4] = (uint8_t)(val[2] & BMI3_SET_LOW_BYTE);
	reg_data[5] = (uint8_t)((val[2] & 0x1F00) >> 8);

	/* Set the configuration to the feature engine register */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX_DATA, reg_data,
		     6));

	/* Update the offset to the sensor engine */
	reg_data[0] = (uint8_t)(BMI3_CMD_USR_GAIN_OFFS_UPDATE &
				 BMI3_SET_LOW_BYTE);

	reg_data[1] = (uint8_t)((BMI3_CMD_USR_GAIN_OFFS_UPDATE &
				BMI3_SET_HIGH_BYTE) >> 8);

	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_CMD, reg_data, 2));

	return EC_SUCCESS;
}

static int wait_and_read_data(const struct motion_sensor_t *s,
			      intv3_t accel_data)
{
	uint8_t reg_data[8] = {0};

	/* Retry 5 times */
	uint8_t try_cnt = FOC_TRY_COUNT;

	/* Check if data is ready */
	while (try_cnt && (!(reg_data[2] & BMI3_STAT_DATA_RDY_ACCEL_MSK))) {
		/* 20ms delay for 50Hz ODR */
		msleep(FOC_DELAY);

		/* Read the status register */
		RETURN_ERROR(bmi3_read_n(s, BMI3_REG_STATUS, reg_data, 4));
		try_cnt--;
	}

	if (!(reg_data[2] & BMI3_STAT_DATA_RDY_ACCEL_MSK))
		return EC_ERROR_TIMEOUT;

	/* Read the sensor data */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_ACC_DATA_X, reg_data, 8));

	accel_data[0] = ((int16_t)((reg_data[3] << 8) | reg_data[2]));
	accel_data[1] = ((int16_t)((reg_data[5] << 8) | reg_data[4]));
	accel_data[2] = ((int16_t)((reg_data[7] << 8) | reg_data[6]));

	rotate(accel_data, *s->rot_standard_ref, accel_data);

	return EC_SUCCESS;
}

/*!
 * @brief This internal API performs Fast Offset Compensation for accelerometer.
 */
static int8_t perform_accel_foc(struct motion_sensor_t *s, int *target,
				int sens_range)
{
	intv3_t accel_data, offset;
	int32_t delta_value[3] = {0, 0, 0};

	/* Variable to define count */
	uint8_t i, loop, sample_count = 0;

	for (loop = 0; loop < BMI3_FOC_SAMPLE_LIMIT; loop++) {

		RETURN_ERROR(wait_and_read_data(s, accel_data));

		sample_count++;

		/* Store the data in a temporary structure */
		delta_value[0] += accel_data[0] - target[X];
		delta_value[1] += accel_data[1] - target[Y];
		delta_value[2] += accel_data[2] - target[Z];
	}

	/* The data is in LSB so -> [(LSB)*1000*range/2^15] (mdps | mg) */
	for (i = X; i <= Z; ++i) {
		offset[i] = (((int64_t)(delta_value[i] * 1000 * sens_range
			     / sample_count) >> 15) * -1);
	}

	rotate_inv(offset, *s->rot_standard_ref, offset);

	RETURN_ERROR(set_accel_offset(s, offset));

	return EC_SUCCESS;
}

static int set_gyro_foc_config(struct motion_sensor_t *s)
{
	uint8_t reg_data[4] = { 0 };
	uint8_t base_addr[2] = { BMI3_BASE_ADDR_SC, 0 };

	/*
	 * Set the user accel offset base address to feature engine
	 * transmission address to start DMA transaction
	 */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX, base_addr, 2));

	/* Read the configuration from the feature engine register */
	RETURN_ERROR(bmi3_read_n(s, BMI3_FEATURE_ENGINE_DMA_TX_DATA, reg_data,
			4));
	/* Enable self calibration */
	reg_data[2] |= 0x07;

	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX, base_addr, 2));

	/* Set the configuration to the feature engine register */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX_DATA,
			&reg_data[2], 2));

	/* Trigger bmi3 gyro self calibration */
	reg_data[0] = (uint8_t)(BMI3_CMD_SELF_CALIB & BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((BMI3_CMD_SELF_CALIB & BMI3_SET_HIGH_BYTE)
				>> 8);

	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_CMD, reg_data, 2));

	return EC_SUCCESS;
}

static int get_calib_result(struct motion_sensor_t *s)
{
	uint8_t i, reg_data[4];

	for (i = 0; i < 25; i++) {
		/* A delay of 120ms is required to read this status register */
		msleep(120);

		/* Read the configuration from the feature engine register */
		RETURN_ERROR(bmi3_read_n(s, BMI3_FEATURE_IO_1, reg_data, 4));

		/* Check calibration complete status */
		if (reg_data[2] & BMI3_SC_ST_STATUS_MASK) {
			/* Check cailbration result */
			if (reg_data[2] & BMI3_SC_RESULT_MASK)
				return EC_SUCCESS;
		}
	}

	return EC_ERROR_NOT_CALIBRATED;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	int ret;
	intv3_t target = {0, 0, 0};
	uint8_t saved_conf[4] = {0};

	/* Sensor is configured to be in 16G range */
	int sens_range = 16;

	/* Variable to set the accelerometer configuration value 50Hz for FOC */
	uint8_t acc_conf_data[2] = {BMI3_FOC_ACC_CONF_VAL_LSB,
					BMI3_FOC_ACC_CONF_VAL_MSB};

	if (!enable)
		return EC_SUCCESS;

	/* Get default configurations for the type of feature selected. */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_ACC_CONF + s->type, saved_conf,
				4));

	/* Set the FOC configuration and add a delay */
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_ACC_CONF, acc_conf_data, 2));
	msleep(FOC_DELAY);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		target[Z] = BMI3_ACC_DATA_PLUS_1G(sens_range);

		/* Perform accel calibration */
		ret = perform_accel_foc(s, target, sens_range);
		if (ret)
			goto end_calib;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		ret = set_gyro_foc_config(s);
		if (ret)
			goto end_calib;

		ret = get_calib_result(s);
		if (ret)
			goto end_calib;
		break;
	default:
		/* Not supported on Magnetometer */
		ret = EC_RES_INVALID_PARAM;
		goto end_calib;
	}


end_calib:
	bmi3_write_n(s, BMI3_REG_ACC_CONF + s->type, &saved_conf[2], 2);

	return ret;
}

static int get_offset(const struct motion_sensor_t *s, int16_t *offset,
		    int16_t *temp)
{
	int i;
	intv3_t v;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/*
		 * The offset of the accelerometer is a 8 bit
		 * two-complement number in units of 3.9 mg independent of the
		 * range selected for the accelerometer.
		 */
		RETURN_ERROR(get_accel_offset(s, v));
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* Gyro offset is in milli-dps */
		RETURN_ERROR(get_gyro_offset(s, v));
		break;
	default:
		for (i = X; i <= Z; i++)
			v[i] = 0;
	}

	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];
	/* Saving temperature at calibration not supported yet */
	*temp = (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP;

	return EC_SUCCESS;
}

static int set_offset(const struct motion_sensor_t *s,
		      const int16_t *offset,
		      int16_t    temp)
{
	intv3_t v = { offset[X], offset[Y], offset[Z] };
	(void)temp;

	rotate_inv(v, *s->rot_standard_ref, v);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* Offset should be in units of mg */
		RETURN_ERROR(set_accel_offset(s, v));
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* Offset should be in units of mdps */
		RETURN_ERROR(set_gyro_offset(s, v));
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_BODY_DETECTION
int get_rms_noise(const struct motion_sensor_t *s)
{
	return EC_ERROR_UNIMPLEMENTED;
}
#endif

static int set_scale(const struct motion_sensor_t *s, const uint16_t *scale,
		     int16_t temp)
{
	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	saved_data->scale[X] = scale[X];
	saved_data->scale[Y] = scale[Y];
	saved_data->scale[Z] = scale[Z];

	return EC_SUCCESS;
}

static int get_scale(const struct motion_sensor_t *s, uint16_t *scale,
		   int16_t *temp)
{
	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	scale[X] = saved_data->scale[X];
	scale[Y] = saved_data->scale[Y];
	scale[Z] = saved_data->scale[Z];

	*temp = (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP;

	return EC_SUCCESS;
}


static int get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	return saved_data->odr;
}

static int set_data_rate(const struct motion_sensor_t *s,
			 int rate, int rnd)
{
	int ret;
	int normalized_rate;
	uint8_t reg_data[4];
	uint8_t reg_val;

	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	RETURN_ERROR(bmi_get_normalized_rate(s, rate, rnd, &normalized_rate,
					     &reg_val));

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	/*
	 * Get default configurations for the type of feature selected.
	 */
	ret = bmi3_read_n(s, BMI3_REG_ACC_CONF + s->type, reg_data, 4);
	if (ret) {
		mutex_unlock(s->mutex);
		return ret;
	}

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		if (rate == 0) {
			/* FIFO stop collecting events */
			if (IS_ENABLED(CONFIG_ACCEL_FIFO))
				ret = enable_fifo(s, 0);

			/* Set the sensor in suspend mode */
			reg_data[3] = BMI3_SET_BITS(reg_data[3],
				BMI3_POWER_MODE,
				BMI3_ACC_MODE_LOW_PWR);

			saved_data->odr = 0;
		} else if (saved_data->odr == 0) {
			/* Power mode changed from suspend to
			 * normal
			 */
			reg_data[3] = BMI3_SET_BITS(reg_data[3],
				      BMI3_POWER_MODE,
				      BMI3_ACC_MODE_NORMAL);
		}

		/* Set accelerometer ODR */
		reg_data[2] = BMI3_SET_BIT_POS0(reg_data[2],
				BMI3_SENS_ODR, reg_val);
	} else if (s->type == MOTIONSENSE_TYPE_GYRO) {
		if (rate == 0) {
			/* FIFO stop collecting events */
			if (IS_ENABLED(CONFIG_ACCEL_FIFO))
				ret = enable_fifo(s, 0);

			/* Set the sensor in suspend mode */
			reg_data[3] = BMI3_SET_BITS(reg_data[3],
				      BMI3_POWER_MODE,
				      BMI3_GYR_MODE_SUSPEND);

			saved_data->odr = 0;
		} else if (saved_data->odr == 0) {
			/* Power mode changed from suspend to
			 * normal
			 */
			reg_data[3] = BMI3_SET_BITS(reg_data[3],
				      BMI3_POWER_MODE,
				      BMI3_GYR_MODE_NORMAL);
		}
		reg_data[2] = BMI3_SET_BIT_POS0(reg_data[2], BMI3_SENS_ODR,
						reg_val);
	}

	/* Set the accel/gyro configurations. */
	ret = bmi3_write_n(s, BMI3_REG_ACC_CONF + s->type, &reg_data[2], 2);

	if (ret == EC_SUCCESS) {
		saved_data->odr = normalized_rate;

		/*
		 * FIFO start collecting events.
		 * They will be discarded if AP does not want them.
		 */
		ret = enable_fifo(s, 1);
	}

	mutex_unlock(s->mutex);
	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return BMI3_16_BIT_RESOLUTION;
}

static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int ret;
	uint8_t index, sens_size = 0;
	uint8_t reg_data[4] = { 0 };
	int (*sensor_range)[2];

	int acc_sensor_range[4][2] = {
		{ 2, BMI3_ACC_RANGE_2G },
		{ 4, BMI3_ACC_RANGE_4G },
		{ 8, BMI3_ACC_RANGE_8G },
		{ 16, BMI3_ACC_RANGE_16G },
	};

	int gyr_sensor_range[5][2] = {
		{ 125, BMI3_GYR_RANGE_125DPS },
		{ 250, BMI3_GYR_RANGE_250DPS },
		{ 500, BMI3_GYR_RANGE_500DPS },
		{ 1000, BMI3_GYR_RANGE_1000DPS },
		{ 2000, BMI3_GYR_RANGE_2000DPS },
	};

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		sens_size = ARRAY_SIZE(acc_sensor_range);

		sensor_range = acc_sensor_range;
	} else {
		sens_size = ARRAY_SIZE(gyr_sensor_range);

		sensor_range = gyr_sensor_range;
	}

	for (index = 0; index < sens_size - 1; index++) {
		if (range <= sensor_range[index][0])
			break;

		if (range < sensor_range[index + 1][0]) {
			if (rnd)
				index += 1;

			break;
		}
	}

	mutex_lock(s->mutex);

	/*
	 * Read the range register from sensor for accelerometer/gyroscope
	 * s->type should have MOTIONSENSE_TYPE_ACCEL = 0 ;
	 * MOTIONSENSE_TYPE_GYRO = 1
	 */
	ret = bmi3_read_n(s, BMI3_REG_ACC_CONF + s->type, reg_data, 4);

	if (ret == EC_SUCCESS) {
		/* Set accelerometer/Gyroscope range */
		/* Gravity range of the sensor (+/- 2G, 4G, 8G, 16G). */
		reg_data[2] = BMI3_SET_BITS(reg_data[2], BMI3_SENS_RANGE,
					    sensor_range[index][1]);

		/* Set the accel/gyro configurations. */
		ret = bmi3_write_n(s, BMI3_REG_ACC_CONF + s->type,
				   &reg_data[2], 2);

		/* Now that we have set the range, update the driver's value. */
		if (ret == EC_SUCCESS)
			s->current_range = sensor_range[index][0];
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret;
	uint8_t reg_data[8] = { 0 };
	uint16_t status_val = 0;

	mutex_lock(s->mutex);

	/* Read the status register */
	ret = bmi3_read_n(s, BMI3_REG_STATUS, reg_data, 4);

	if (ret == EC_SUCCESS) {
		status_val = (reg_data[2] | ((uint16_t)reg_data[3] << 8));
		/*
		 * If sensor data is not ready, return the previous read data.
		 * Note: return success so that motion sensor task can read
		 * again to get the latest updated sensor data quickly.
		 */
		if (!(status_val & BMI3_DRDY_MASK(s->type))) {
			if (v != s->raw_xyz)
				memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));

			mutex_unlock(s->mutex);

			return EC_SUCCESS;
		}

		if (s->type == MOTIONSENSE_TYPE_ACCEL) {
			/* Read the sensor data */
			ret = bmi3_read_n(s, BMI3_REG_ACC_DATA_X, reg_data, 8);
		} else if (s->type == MOTIONSENSE_TYPE_GYRO) {
			/* Read the sensor data */
			ret = bmi3_read_n(s, BMI3_REG_GYR_DATA_X, reg_data, 8);
		}

		if (ret == EC_SUCCESS) {
			v[0] = ((int16_t)((reg_data[3] << 8) | reg_data[2]));
			v[1] = ((int16_t)((reg_data[5] << 8) | reg_data[4]));
			v[2] = ((int16_t)((reg_data[7] << 8) | reg_data[6]));

			rotate(v, *s->rot_standard_ref, v);
		}
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int init(struct motion_sensor_t *s)
{
	/* Status of communication result */
	uint8_t i;
	uint8_t reg_data[4] = { 0 };

	/* Store the sensor configurations */
	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);

	/* This driver requires a mutex */
	ASSERT(s->mutex);

	/* Reset bmi3 device */
	reg_data[0] = (uint8_t)(BMI3_CMD_SOFT_RESET & BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((BMI3_CMD_SOFT_RESET & BMI3_SET_HIGH_BYTE)
				>> 8);

	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_CMD, reg_data, 2));

	/* Delay of 2ms after soft reset*/
	msleep(2);

	/* Enable feature engine bit */
	reg_data[0] = BMI3_ENABLE;
	reg_data[1] = 0;

	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_FEATURE_ENGINE_GLOB_CTRL,
				  reg_data, 2));

	/* Read chip id */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_CHIP_ID, reg_data, 4));

	if (reg_data[2] != BMI323_CHIP_ID)
		return EC_ERROR_HW_INTERNAL;

	for (i = X; i <= Z; i++)
		saved_data->scale[i] = MOTION_SENSE_DEFAULT_SCALE;

	/* The sensor is in Suspend mode at init, so set data rate to 0*/
	saved_data->odr = 0;

	/* Flags used in FIFO parsing */
	data->flags &= ~(BMI_FLAG_SEC_I2C_ENABLED
			| (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET));

	if (IS_ENABLED(CONFIG_ACCEL_INTERRUPTS)
	    && (s->type == MOTIONSENSE_TYPE_ACCEL))
		RETURN_ERROR(config_interrupt(s));

	return sensor_init_done(s);
}

/* Accelerometer/Gyroscope base driver structure */
const struct accelgyro_drv bmi3xx_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.get_scale = get_scale,
	.set_scale = set_scale,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = perform_calib,
	.read_temp = read_temp,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = irq_handler,
#endif
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
};
