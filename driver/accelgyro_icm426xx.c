/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * ICM-426xx accelerometer and gyroscope module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "console.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_ACCELGYRO_ICM426XX_INT_EVENT
#define ACCELGYRO_ICM426XX_INT_ENABLE
#endif

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCELGYRO_ICM426XX_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

static int icm426xx_normalize(const struct motion_sensor_t *s, intv3_t v,
			      const uint8_t *raw)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);
	int i;

	/* sensor data is configured as little-endian */
	v[X] = (int16_t)UINT16_FROM_BYTE_ARRAY_LE(raw, 0);
	v[Y] = (int16_t)UINT16_FROM_BYTE_ARRAY_LE(raw, 2);
	v[Z] = (int16_t)UINT16_FROM_BYTE_ARRAY_LE(raw, 4);

	/* check if data is valid */
	if (v[X] == ICM426XX_INVALID_DATA && v[Y] == ICM426XX_INVALID_DATA &&
	    v[Z] == ICM426XX_INVALID_DATA) {
		return EC_ERROR_INVAL;
	}

	rotate(v, *s->rot_standard_ref, v);

	for (i = X; i <= Z; i++)
		v[i] = SENSOR_APPLY_SCALE(v[i], data->scale[i]);

	return EC_SUCCESS;
}

static int icm426xx_check_sensor_stabilized(const struct motion_sensor_t *s,
					    uint32_t ts)
{
	int32_t rem;

	rem = icm_get_sensor_stabilized(s, ts);
	if (rem == 0)
		return EC_SUCCESS;
	if (rem > 0)
		return EC_ERROR_BUSY;

	/* rem < 0: reset check since ts has passed stabilize_ts */
	icm_reset_stabilize_ts(s);
	return EC_SUCCESS;
}

/* use FIFO threshold interrupt on INT1 */
#define ICM426XX_FIFO_INT_EN ICM426XX_FIFO_THS_INT1_EN
#define ICM426XX_FIFO_INT_STATUS ICM426XX_FIFO_THS_INT

static int __maybe_unused icm426xx_enable_fifo(const struct motion_sensor_t *s,
					       int enable)
{
	int val, ret;

	if (enable) {
		/* enable FIFO interrupts */
		ret = icm_field_update8(s, ICM426XX_REG_INT_SOURCE0,
					ICM426XX_FIFO_INT_EN,
					ICM426XX_FIFO_INT_EN);
		if (ret != EC_SUCCESS)
			return ret;

		/* flush FIFO data */
		ret = icm_write8(s, ICM426XX_REG_SIGNAL_PATH_RESET,
				 ICM426XX_FIFO_FLUSH);
		if (ret != EC_SUCCESS)
			return ret;

		/* set FIFO in streaming mode */
		ret = icm_write8(s, ICM426XX_REG_FIFO_CONFIG,
				 ICM426XX_FIFO_MODE_STREAM);
		if (ret != EC_SUCCESS)
			return ret;

		/* workaround: first read of FIFO count is always 0 */
		ret = icm_read16(s, ICM426XX_REG_FIFO_COUNT, &val);
		if (ret != EC_SUCCESS)
			return ret;
	} else {
		/* set FIFO in bypass mode */
		ret = icm_write8(s, ICM426XX_REG_FIFO_CONFIG,
				 ICM426XX_FIFO_MODE_BYPASS);
		if (ret != EC_SUCCESS)
			return ret;

		/* flush FIFO data */
		ret = icm_write8(s, ICM426XX_REG_SIGNAL_PATH_RESET,
				 ICM426XX_FIFO_FLUSH);
		if (ret != EC_SUCCESS)
			return ret;

		/* disable FIFO interrupts */
		ret = icm_field_update8(s, ICM426XX_REG_INT_SOURCE0,
					ICM426XX_FIFO_INT_EN, 0);
		if (ret != EC_SUCCESS)
			return ret;
	}

	return EC_SUCCESS;
}

static int __maybe_unused icm426xx_config_fifo(const struct motion_sensor_t *s,
					       int enable)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	int mask, val;
	uint8_t old_fifo_en;
	int ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		mask = ICM426XX_FIFO_ACCEL_EN;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		mask = ICM426XX_FIFO_GYRO_EN;
		break;
	default:
		return EC_ERROR_INVAL;
	}
	/* temperature data has to be always present in the FIFO */
	mask |= ICM426XX_FIFO_TEMP_EN;

	val = enable ? mask : 0;

	mutex_lock(s->mutex);

	ret = icm_field_update8(s, ICM426XX_REG_FIFO_CONFIG1, mask, val);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	old_fifo_en = st->fifo_en;
	if (enable)
		st->fifo_en |= BIT(s->type);
	else
		st->fifo_en &= ~BIT(s->type);

	if (!old_fifo_en && st->fifo_en) {
		/* 1st sensor enabled => turn FIFO on */
		ret = icm426xx_enable_fifo(s, 1);
		if (ret != EC_SUCCESS)
			goto out_unlock;
	} else if (old_fifo_en && !st->fifo_en) {
		/* last sensor disabled => turn FIFO off */
		ret = icm426xx_enable_fifo(s, 0);
		if (ret != EC_SUCCESS)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static void __maybe_unused icm426xx_push_fifo_data(struct motion_sensor_t *s,
						   const uint8_t *raw,
						   uint32_t ts)
{
	struct ec_response_motion_sensor_data vect;
	int *v = s->raw_xyz;

	if (icm426xx_normalize(s, v, raw) != EC_SUCCESS)
		return;

	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
	    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
		v = s->spoof_xyz;
	if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		vect.data[X] = v[X];
		vect.data[Y] = v[Y];
		vect.data[Z] = v[Z];
		vect.flags = 0;
		vect.sensor_num = s - motion_sensors;
		motion_sense_fifo_stage_data(&vect, s, 3, ts);
	} else {
		motion_sense_push_raw_xyz(s);
	}
}

static int __maybe_unused icm426xx_load_fifo(struct motion_sensor_t *s,
					     uint32_t ts)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	int count, i, size;
	const uint8_t *accel, *gyro;
	int ret;

	ret = icm_read16(s, ICM426XX_REG_FIFO_COUNT, &count);
	if (ret != EC_SUCCESS)
		return ret;

	if (count <= 0)
		return EC_ERROR_INVAL;

	/* flush FIFO if buffer is not large enough */
	if (count > ICM_FIFO_BUFFER) {
		CPRINTS("It should not happen, the EC is too slow for the ODR");
		RETURN_ERROR(icm_write8(s, ICM426XX_REG_SIGNAL_PATH_RESET,
					ICM426XX_FIFO_FLUSH));
		return EC_ERROR_OVERFLOW;
	}

	ret = icm_read_n(s, ICM426XX_REG_FIFO_DATA, st->fifo_buffer, count);
	if (ret != EC_SUCCESS)
		return ret;

	for (i = 0; i < count; i += size) {
		size = icm_fifo_decode_packet(&st->fifo_buffer[i], &accel,
					      &gyro);
		/* exit if error or FIFO is empty */
		if (size <= 0)
			return -size;
		if (accel != NULL) {
			ret = icm426xx_check_sensor_stabilized(st->accel, ts);
			if (ret == EC_SUCCESS)
				icm426xx_push_fifo_data(st->accel, accel, ts);
		}
		if (gyro != NULL) {
			ret = icm426xx_check_sensor_stabilized(st->gyro, ts);
			if (ret == EC_SUCCESS)
				icm426xx_push_fifo_data(st->gyro, gyro, ts);
		}
	}

	return EC_SUCCESS;
}

#ifdef ACCELGYRO_ICM426XX_INT_ENABLE

/**
 * icm426xx_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->icm426xx_irq_handler().
 */
void icm426xx_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ACCELGYRO_ICM426XX_INT_EVENT);
}

/**
 * icm426xx_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 */
static int icm426xx_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int status;
	int ret;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCELGYRO_ICM426XX_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	mutex_lock(s->mutex);

	/* read and clear interrupt status */
	ret = icm_read8(s, ICM426XX_REG_INT_STATUS, &status);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	if (status & ICM426XX_FIFO_INT_STATUS) {
		ret = icm426xx_load_fifo(s, last_interrupt_timestamp);
		if (IS_ENABLED(CONFIG_ACCEL_FIFO) && (ret == EC_SUCCESS))
			motion_sense_fifo_commit_data();
	}

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm426xx_config_interrupt(const struct motion_sensor_t *s)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	int val, ret;

	/* configure INT1 pin */
	RETURN_ERROR(icm_write8(s, ICM426XX_REG_INT_CONFIG,
				ICM426XX_INT1_PUSH_PULL));

	/* deassert async reset for proper INT pin operation */
	ret = icm_field_update8(s, ICM426XX_REG_INT_CONFIG1,
				ICM426XX_INT_ASYNC_RESET, 0);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * configure FIFO:
	 * - enable FIFO partial read
	 * - enable continuous watermark interrupt
	 * - disable all FIFO en bits
	 */
	val = ICM426XX_FIFO_PARTIAL_READ | ICM426XX_FIFO_WM_GT_TH;
	ret = icm_field_update8(s, ICM426XX_REG_FIFO_CONFIG1,
				GENMASK(6, 5) | ICM426XX_FIFO_EN_MASK, val);
	if (ret != EC_SUCCESS)
		return ret;

	/* clear internal FIFO enable bits tracking */
	st->fifo_en = 0;

	/* set FIFO watermark to 1 data packet (8 bytes) */
	ret = icm_write16(s, ICM426XX_REG_FIFO_WATERMARK, 8);
	if (ret != EC_SUCCESS)
		return ret;
	return ret;
}

#endif /* ACCELGYRO_ICM426XX_INT_ENABLE */

static int icm426xx_enable_sensor(const struct motion_sensor_t *s, int enable)
{
	uint32_t delay, stop_delay;
	int32_t rem;
	uint8_t mask, val;
	int ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		mask = ICM426XX_ACCEL_MODE_MASK;
		if (enable) {
			delay = ICM426XX_ACCEL_START_TIME;
			stop_delay = ICM426XX_ACCEL_STOP_TIME;
			val = ICM426XX_ACCEL_MODE(ICM426XX_MODE_LOW_POWER);
		} else {
			delay = ICM426XX_ACCEL_STOP_TIME;
			val = ICM426XX_ACCEL_MODE(ICM426XX_MODE_OFF);
		}
		break;
	case MOTIONSENSE_TYPE_GYRO:
		mask = ICM426XX_GYRO_MODE_MASK;
		if (enable) {
			delay = ICM426XX_GYRO_START_TIME;
			stop_delay = ICM426XX_GYRO_STOP_TIME;
			val = ICM426XX_GYRO_MODE(ICM426XX_MODE_LOW_NOISE);
		} else {
			delay = ICM426XX_GYRO_STOP_TIME;
			val = ICM426XX_GYRO_MODE(ICM426XX_MODE_OFF);
		}
		break;
	default:
		return EC_ERROR_INVAL;
	}

	/* check stop delay and sleep if required */
	if (enable) {
		rem = icm_get_sensor_stabilized(s, __hw_clock_source_read());
		/* rem > stop_delay means counter rollover */
		if (rem > 0 && rem <= stop_delay)
			crec_usleep(rem);
	}

	mutex_lock(s->mutex);

	ret = icm_field_update8(s, ICM426XX_REG_PWR_MGMT0, mask, val);
	if (ret == EC_SUCCESS) {
		icm_set_stabilize_ts(s, delay);
		/* when turning sensor on block any register write for 200 us */
		if (enable)
			crec_usleep(200);
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int icm426xx_set_data_rate(const struct motion_sensor_t *s, int rate,
				  int rnd)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);
	int reg, ret, reg_val;
	int normalized_rate;
	int max_rate, min_rate;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM426XX_REG_ACCEL_CONFIG0;
		min_rate = ICM426XX_ACCEL_MIN_FREQ;
		max_rate = ICM426XX_ACCEL_MAX_FREQ;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM426XX_REG_GYRO_CONFIG0;
		min_rate = ICM426XX_GYRO_MIN_FREQ;
		max_rate = ICM426XX_GYRO_MAX_FREQ;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	/* normalize the rate */
	reg_val = ICM426XX_ODR_TO_REG(rate);
	normalized_rate = ICM426XX_REG_TO_ODR(reg_val);

	/* round up the rate */
	if (rnd && (normalized_rate < rate)) {
		reg_val = ICM426XX_ODR_REG_UP(reg_val);
		normalized_rate = ICM426XX_REG_TO_ODR(reg_val);
	}

	if (rate > 0) {
		if ((normalized_rate < min_rate) ||
		    (normalized_rate > max_rate))
			return EC_RES_INVALID_PARAM;
	}

	if (rate == 0) {
		/* disable data in FIFO */
		icm426xx_config_fifo(s, 0);
		/* disable sensor */
		ret = icm426xx_enable_sensor(s, 0);
		data->odr = 0;
		return ret;
	}

	mutex_lock(s->mutex);

	ret = icm_field_update8(s, reg, ICM426XX_ODR_MASK,
				ICM426XX_ODR(reg_val));
	if (ret != EC_SUCCESS)
		goto out_unlock;

	mutex_unlock(s->mutex);

	if (data->odr == 0) {
		/* enable sensor */
		ret = icm426xx_enable_sensor(s, 1);
		if (ret)
			return ret;
		/* enable data in FIFO */
		icm426xx_config_fifo(s, 1);
	}

	data->odr = normalized_rate;
	return EC_SUCCESS;

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm426xx_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int reg, ret, reg_val;
	int newrange;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM426XX_REG_ACCEL_CONFIG0;

		reg_val = ICM426XX_ACCEL_FS_TO_REG(range);
		newrange = ICM426XX_ACCEL_REG_TO_FS(reg_val);

		if (rnd && (newrange < range) && (reg_val > 0)) {
			reg_val--;
			newrange = ICM426XX_ACCEL_REG_TO_FS(reg_val);
		}

		if (newrange > ICM426XX_ACCEL_FS_MAX_VAL) {
			newrange = ICM426XX_ACCEL_FS_MAX_VAL;
			reg_val = ICM426XX_ACCEL_FS_TO_REG(range);
		}

		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM426XX_REG_GYRO_CONFIG0;

		reg_val = ICM426XX_GYRO_FS_TO_REG(range);
		newrange = ICM426XX_GYRO_REG_TO_FS(reg_val);

		if (rnd && (newrange < range) && (reg_val > 0)) {
			reg_val--;
			newrange = ICM426XX_GYRO_REG_TO_FS(reg_val);
		}

		if (newrange > ICM426XX_GYRO_FS_MAX_VAL) {
			newrange = ICM426XX_GYRO_FS_MAX_VAL;
			reg_val = ICM426XX_GYRO_FS_TO_REG(newrange);
		}

		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	mutex_lock(s->mutex);

	ret = icm_field_update8(s, reg, ICM426XX_FS_MASK,
				ICM426XX_FS_SEL(reg_val));
	if (ret == EC_SUCCESS)
		s->current_range = newrange;

	mutex_unlock(s->mutex);

	return ret;
}

static int icm426xx_get_hw_offset(const struct motion_sensor_t *s,
				  intv3_t offset)
{
	uint8_t raw[5];
	int i, ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		mutex_lock(s->mutex);
		ret = icm_read_n(s, ICM426XX_REG_OFFSET_USER4, raw,
				 sizeof(raw));
		mutex_unlock(s->mutex);
		if (ret != EC_SUCCESS)
			return ret;
		/*
		 * raw[0]: Accel X[11:8] | gyro Z[11:8]
		 * raw[1]: Accel X[0:7]
		 * raw[2]: Accel Y[7:0]
		 * raw[3]: Accel Z[11:8] | Accel Y[11:8]
		 * raw[4]: Accel Z[7:0]
		 */
		offset[X] = (((int)raw[0] << 4) & ~GENMASK(7, 0)) | raw[1];
		offset[Y] = (((int)raw[3] << 8) & ~GENMASK(7, 0)) | raw[2];
		offset[Z] = (((int)raw[3] << 4) & ~GENMASK(7, 0)) | raw[4];
		break;
	case MOTIONSENSE_TYPE_GYRO:
		mutex_lock(s->mutex);
		ret = icm_read_n(s, ICM426XX_REG_OFFSET_USER0, raw,
				 sizeof(raw));
		mutex_unlock(s->mutex);
		if (ret != EC_SUCCESS)
			return ret;
		/*
		 * raw[0]: Gyro X[7:0]
		 * raw[1]: Gyro Y[11:8] | Gyro X[11:8]
		 * raw[2]: Gyro Y[7:0]
		 * raw[3]: Gyro Z[7:0]
		 * raw[4]: Accel X[11:8] | gyro Z[11:8]
		 */
		offset[X] = (((int)raw[1] << 8) & ~GENMASK(7, 0)) | raw[0];
		offset[Y] = (((int)raw[1] << 4) & ~GENMASK(7, 0)) | raw[2];
		offset[Z] = (((int)raw[4] << 8) & ~GENMASK(7, 0)) | raw[3];
		break;
	default:
		return EC_ERROR_INVAL;
	}

	/* Extend sign-bit of 12 bits signed values */
	for (i = X; i <= Z; ++i)
		offset[i] = icm_sign_extend(offset[i], 11);

	return EC_SUCCESS;
}

static int icm426xx_set_hw_offset(const struct motion_sensor_t *s,
				  intv3_t offset)
{
	int i, val, ret;

	/* value is 12 bits signed maximum */
	for (i = X; i <= Z; ++i) {
		if (offset[i] > 2047)
			offset[i] = 2047;
		else if (offset[i] < -2048)
			offset[i] = -2048;
	}

	mutex_lock(s->mutex);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* Accel X[11:8] | gyro Z[11:8] */
		val = (offset[X] >> 4) & GENMASK(7, 4);
		ret = icm_field_update8(s, ICM426XX_REG_OFFSET_USER4,
					GENMASK(7, 4), val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel X[7:0] */
		val = offset[X] & GENMASK(7, 0);
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER5, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel Y[7:0] */
		val = offset[Y] & GENMASK(7, 0);
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER6, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel Z[11:8] | Accel Y[11:8] */
		val = ((offset[Z] >> 4) & GENMASK(7, 4)) |
		      ((offset[Y] >> 8) & GENMASK(3, 0));
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER7, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel Z[7:0] */
		val = offset[Z] & GENMASK(7, 0);
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER8, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;
		break;

	case MOTIONSENSE_TYPE_GYRO:
		/* Gyro X[7:0] */
		val = offset[X] & GENMASK(7, 0);
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER0, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Gyro Y[11:8] | Gyro X[11:8] */
		val = ((offset[Y] >> 4) & GENMASK(7, 4)) |
		      ((offset[X] >> 8) & GENMASK(3, 0));
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER1, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Gyro Y[7:0] */
		val = offset[Y] & GENMASK(7, 0);
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER2, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Gyro Z[7:0] */
		val = offset[Z] & GENMASK(7, 0);
		ret = icm_write8(s, ICM426XX_REG_OFFSET_USER3, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel X[11:8] | gyro Z[11:8] */
		val = (offset[Z] >> 8) & GENMASK(3, 0);
		ret = icm_field_update8(s, ICM426XX_REG_OFFSET_USER4,
					GENMASK(3, 0), val);
		if (ret != EC_SUCCESS)
			goto out_unlock;
		break;

	default:
		ret = EC_ERROR_INVAL;
		break;
	}

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm426xx_set_offset(const struct motion_sensor_t *s,
			       const int16_t *offset, int16_t temp)
{
	intv3_t v = { offset[X], offset[Y], offset[Z] };
	int div1, div2;
	int i;

	/* rotate back to chip frame */
	rotate_inv(v, *s->rot_standard_ref, v);

	/* convert raw data to hardware offset units */
	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* hardware offset is 1/2048g /LSB, EC offset 1/1024g /LSB. */
		div1 = 2;
		div2 = 1;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* hardware offset is 1/32dps /LSB, EC offset 1/1024dps /LSB. */
		div1 = 1;
		div2 = 32;
		break;
	default:
		return EC_ERROR_INVAL;
	}
	for (i = X; i <= Z; ++i)
		v[i] = round_divide(v[i] * div1, div2);

	return icm426xx_set_hw_offset(s, v);
}

static int icm426xx_get_offset(const struct motion_sensor_t *s, int16_t *offset,
			       int16_t *temp)
{
	intv3_t v;
	int div1, div2;
	int i, ret;

	ret = icm426xx_get_hw_offset(s, v);
	if (ret != EC_SUCCESS)
		return ret;

	/* transform offset to raw data */
	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* hardware offset is 1/2048g /LSB, EC offset 1/1024g /LSB. */
		div1 = 1;
		div2 = 2;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* hardware offset is 1/32dps /LSB, EC offset 1/1024dps /LSB. */
		div1 = 32;
		div2 = 1;
		break;
	default:
		return EC_ERROR_INVAL;
	}
	for (i = X; i <= Z; ++i)
		v[i] = round_divide(v[i] * div1, div2);

	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int icm426xx_read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[6];
	int reg, ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM426XX_REG_ACCEL_DATA_XYZ;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM426XX_REG_GYRO_DATA_XYZ;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	/* read data registers if sensor is stabilized */
	mutex_lock(s->mutex);

	ret = icm426xx_check_sensor_stabilized(s, __hw_clock_source_read());
	if (ret == EC_SUCCESS)
		ret = icm_read_n(s, reg, raw, sizeof(raw));

	mutex_unlock(s->mutex);
	if (ret != EC_SUCCESS)
		return ret;

	ret = icm426xx_normalize(s, v, raw);
	/* if data is invalid return the previous read data */
	if (ret != EC_SUCCESS) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
	}

	return EC_SUCCESS;
}

static int icm426xx_read_temp(const struct motion_sensor_t *s, int *temp_ptr)
{
	int val, ret;

	mutex_lock(s->mutex);
	ret = icm_read16(s, ICM426XX_REG_TEMP_DATA, &val);
	mutex_unlock(s->mutex);
	if (ret != EC_SUCCESS)
		return ret;

	/* ensure correct propagation of 16 bits sign bit */
	val = icm_sign_extend(val, 15);

	if (val == ICM426XX_INVALID_DATA)
		return EC_ERROR_NOT_POWERED;

	*temp_ptr = C_TO_K((val * 100) / 13248 + 25);
	return EC_SUCCESS;
}

static int icm426xx_init_config(const struct motion_sensor_t *s)
{
	uint8_t mask, val;
	int ret;

	/*
	 * serial bus setup (i2c or spi)
	 *
	 * Do not check result for INTF_CONFIG6, since it can induce
	 * interferences on the bus.
	 */

#ifdef CONFIG_ACCELGYRO_ICM_COMM_SPI
	icm_field_update8(
		s, ICM426XX_REG_INTF_CONFIG6, ICM426XX_INTF_CONFIG6_MASK,
		ICM426XX_I3C_EN | ICM426XX_I3C_SDR_EN | ICM426XX_I3C_DDR_EN);
	ret = icm_field_update8(s, ICM426XX_REG_INTF_CONFIG4,
				ICM426XX_I3C_BUS_MODE, ICM426XX_I3C_BUS_MODE);
#else
	icm_field_update8(s, ICM426XX_REG_INTF_CONFIG6,
			  ICM426XX_INTF_CONFIG6_MASK, ICM426XX_I3C_EN);
	ret = icm_field_update8(s, ICM426XX_REG_INTF_CONFIG4,
				ICM426XX_I3C_BUS_MODE, 0);
#endif
	if (ret)
		return ret;

#ifdef CONFIG_ACCELGYRO_ICM_COMM_SPI
	ret = icm_field_update8(
		s, ICM426XX_REG_DRIVE_CONFIG, ICM426XX_DRIVE_CONFIG_MASK,
		ICM426XX_I2C_SLEW_RATE(ICM426XX_SLEW_RATE_20NS_60NS) |
			ICM426XX_SPI_SLEW_RATE(ICM426XX_SLEW_RATE_INF_2NS));
#else
	ret = icm_field_update8(
		s, ICM426XX_REG_DRIVE_CONFIG, ICM426XX_DRIVE_CONFIG_MASK,
		ICM426XX_I2C_SLEW_RATE(ICM426XX_SLEW_RATE_12NS_36NS) |
			ICM426XX_SPI_SLEW_RATE(ICM426XX_SLEW_RATE_12NS_36NS));
#endif
	if (ret)
		return ret;

	/*
	 * Use invalid value in registers and FIFO.
	 * Data registers in little-endian format.
	 * Disable unused serial interface.
	 */
	mask = ICM426XX_DATA_CONF_MASK | ICM426XX_UI_SIFS_CFG_MASK;
#ifdef CONFIG_ACCELGYRO_ICM_COMM_SPI
	val = ICM426XX_UI_SIFS_CFG_I2C_DIS;
#else
	val = ICM426XX_UI_SIFS_CFG_SPI_DIS;
#endif

	ret = icm_field_update8(s, ICM426XX_REG_INTF_CONFIG0, mask, val);
	if (ret)
		return ret;

	/* set accel oscillator to RC clock to avoid bad transition with PLL */
	return icm_field_update8(s, ICM426XX_REG_INTF_CONFIG1,
				 ICM426XX_ACCEL_LP_CLK_SEL,
				 ICM426XX_ACCEL_LP_CLK_SEL);
}

static int icm426xx_init(struct motion_sensor_t *s)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	struct accelgyro_saved_data_t *saved_data = ICM_GET_SAVED_DATA(s);
	int mask, val;
	int ret, i;

	mutex_lock(s->mutex);

	/* manually force register bank to 0 */
	st->bank = 0;
	ret = icm_write8(s, ICM426XX_REG_BANK_SEL, ICM426XX_BANK_SEL(0));
	if (ret)
		goto out_unlock;

	/* detect chip using whoami */
	ret = icm_read8(s, ICM426XX_REG_WHO_AM_I, &val);
	if (ret)
		goto out_unlock;

	if (val != ICM426XX_CHIP_ICM40608 && val != ICM426XX_CHIP_ICM42605) {
		CPRINTS("Unknown WHO_AM_I: 0x%02x", val);
		ret = EC_ERROR_ACCESS_DENIED;
		goto out_unlock;
	}

	/* first time init done only for 1st sensor (accel) */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		/* reset the chip and verify it is ready */
		ret = icm_write8(s, ICM426XX_REG_DEVICE_CONFIG,
				 ICM426XX_SOFT_RESET_CONFIG);
		if (ret)
			goto out_unlock;
		crec_msleep(1);

		ret = icm_read8(s, ICM426XX_REG_INT_STATUS, &val);
		if (ret)
			goto out_unlock;
		if (!(val & ICM426XX_RESET_DONE_INT)) {
			ret = EC_ERROR_ACCESS_DENIED;
			goto out_unlock;
		}

		/* configure sensor */
		ret = icm426xx_init_config(s);
		if (ret)
			goto out_unlock;

		if (IS_ENABLED(ACCELGYRO_ICM426XX_INT_ENABLE))
			ret = icm426xx_config_interrupt(s);
		if (ret)
			goto out_unlock;
	}

	for (i = X; i <= Z; i++)
		saved_data->scale[i] = MOTION_SENSE_DEFAULT_SCALE;

	saved_data->odr = 0;

	/* set sensor filter */
	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		mask = ICM426XX_ACCEL_UI_FILT_MASK;
		val = ICM426XX_ACCEL_UI_FILT_BW(ICM426XX_FILTER_BW_AVG_16X);
		st->accel = (struct motion_sensor_t *)s;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		mask = ICM426XX_GYRO_UI_FILT_MASK;
		val = ICM426XX_GYRO_UI_FILT_BW(ICM426XX_FILTER_BW_ODR_DIV_2);
		st->gyro = (struct motion_sensor_t *)s;
		break;
	default:
		ret = EC_ERROR_INVAL;
		goto out_unlock;
	}
	ret = icm_field_update8(s, ICM426XX_REG_GYRO_ACCEL_CONFIG0, mask, val);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	mutex_unlock(s->mutex);

	return sensor_init_done(s);

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm426xx_probe(const struct motion_sensor_t *s)
{
	int val;

	if (icm_read8(s, ICM426XX_REG_WHO_AM_I, &val) != EC_SUCCESS)
		return EC_ERROR_NOT_HANDLED;

	if (val != ICM426XX_CHIP_ICM40608 && val != ICM426XX_CHIP_ICM42605)
		return EC_ERROR_NOT_HANDLED;

	return EC_SUCCESS;
}

const struct accelgyro_drv icm426xx_drv = {
	.init = icm426xx_init,
	.read = icm426xx_read,
	.read_temp = icm426xx_read_temp,
	.set_range = icm426xx_set_range,
	.get_resolution = icm_get_resolution,
	.set_data_rate = icm426xx_set_data_rate,
	.get_data_rate = icm_get_data_rate,
	.set_offset = icm426xx_set_offset,
	.get_offset = icm426xx_get_offset,
	.set_scale = icm_set_scale,
	.get_scale = icm_get_scale,
	.probe = icm426xx_probe,
#ifdef ACCELGYRO_ICM426XX_INT_ENABLE
	.interrupt = icm426xx_interrupt,
	.irq_handler = icm426xx_irq_handler,
#endif
};
