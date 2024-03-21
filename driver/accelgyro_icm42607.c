/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * ICM-42607 accelerometer and gyroscope module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "builtin/endian.h"
#include "console.h"
#include "driver/accelgyro_icm42607.h"
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

#ifdef CONFIG_ACCELGYRO_ICM42607_INT_EVENT
#define ACCELGYRO_ICM42607_INT_ENABLE
#endif

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCELGYRO_ICM42607_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

static int icm_switch_on_mclk(const struct motion_sensor_t *s)
{
	int val;
	int i, ret;

	ret = icm_field_update8(s, ICM42607_REG_PWR_MGMT0, ICM42607_IDLE,
				ICM42607_IDLE);
	if (ret)
		return ret;

	/* Check if MCLK is ready */
	for (i = 0; i < 10; ++i) {
		ret = icm_read8(s, ICM42607_REG_MCLK_RDY, &val);
		if (ret)
			return ret;

		if (val & ICM42607_MCLK_RDY)
			return EC_SUCCESS;
	}

	return EC_ERROR_HW_INTERNAL;
}

static int icm_switch_off_mclk(const struct motion_sensor_t *s)
{
	return icm_field_update8(s, ICM42607_REG_PWR_MGMT0, ICM42607_IDLE, 0);
}

static int icm_read_mclk_reg(const struct motion_sensor_t *s, const int reg,
			     int *buf)
{
	const int blk_sel = (reg & 0xFF00) >> 8;
	const int val = reg & 0x00FF;
	int ret;

	/* optimize by changing BLK_SEL only if not 0 */
	if (blk_sel) {
		ret = icm_write8(s, ICM42607_REG_BLK_SEL_R, blk_sel);
		if (ret)
			return ret;
	}

	ret = icm_write8(s, ICM42607_REG_MADDR_R, val);
	if (ret)
		return ret;

	udelay(10);

	ret = icm_read8(s, ICM42607_REG_M_R, buf);
	if (ret)
		return ret;

	udelay(10);

	if (blk_sel) {
		ret = icm_write8(s, ICM42607_REG_BLK_SEL_R, 0);
		if (ret)
			return ret;
	}

	return EC_SUCCESS;
}

static int icm_write_mclk_reg(const struct motion_sensor_t *s, const int reg,
			      const int buf)
{
	const int blk_sel = (reg & 0xFF00) >> 8;
	const int val = reg & 0x00FF;
	int ret;

	/* optimize by changing BLK_SEL only if not 0 */
	if (blk_sel) {
		ret = icm_write8(s, ICM42607_REG_BLK_SEL_W, blk_sel);
		if (ret)
			return ret;
	}

	ret = icm_write8(s, ICM42607_REG_MADDR_W, val);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_M_W, buf);
	if (ret)
		return ret;

	udelay(10);

	if (blk_sel) {
		ret = icm_write8(s, ICM42607_REG_BLK_SEL_W, 0);
		if (ret)
			return ret;
	}

	return EC_SUCCESS;
}

static int icm_field_update_mclk_reg(const struct motion_sensor_t *s,
				     const int reg, const uint8_t field_mask,
				     const uint8_t set_value)
{
	int val, ret;

	ret = icm_read_mclk_reg(s, reg, &val);
	if (ret)
		return ret;

	val = (val & ~field_mask) | set_value;

	return icm_write_mclk_reg(s, reg, val);
}

static int icm42607_normalize(const struct motion_sensor_t *s, intv3_t v,
			      const uint8_t *raw)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);
	int i;

	/* sensor data is configured as little-endian */
	v[X] = (int16_t)UINT16_FROM_BYTE_ARRAY_LE(raw, 0);
	v[Y] = (int16_t)UINT16_FROM_BYTE_ARRAY_LE(raw, 2);
	v[Z] = (int16_t)UINT16_FROM_BYTE_ARRAY_LE(raw, 4);

	/* check if data is valid */
	if (v[X] == ICM42607_INVALID_DATA && v[Y] == ICM42607_INVALID_DATA &&
	    v[Z] == ICM42607_INVALID_DATA) {
		return EC_ERROR_INVAL;
	}

	rotate(v, *s->rot_standard_ref, v);

	for (i = X; i <= Z; i++)
		v[i] = SENSOR_APPLY_SCALE(v[i], data->scale[i]);

	return EC_SUCCESS;
}

static int icm42607_check_sensor_stabilized(const struct motion_sensor_t *s,
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

static int __maybe_unused icm42607_flush_fifo(const struct motion_sensor_t *s)
{
	int i, val, ret;

	ret = icm_write8(s, ICM42607_REG_SIGNAL_PATH_RESET,
			 ICM42607_FIFO_FLUSH);
	if (ret)
		return ret;

	udelay(10);

	for (i = 0; i < 10; ++i) {
		ret = icm_read8(s, ICM42607_REG_SIGNAL_PATH_RESET, &val);
		if (ret)
			return ret;

		if (!(val & ICM42607_FIFO_FLUSH))
			return EC_SUCCESS;

		udelay(1);
	}

	return EC_ERROR_HW_INTERNAL;
}

/* use FIFO threshold interrupt on INT1 */
#define ICM42607_FIFO_INT_EN ICM42607_FIFO_THS_INT1_EN
#define ICM42607_FIFO_INT_STATUS ICM42607_FIFO_THS_INT

static int __maybe_unused icm42607_enable_fifo(const struct motion_sensor_t *s,
					       int enable)
{
	int ret;

	if (enable) {
		/* enable FIFO interrupts */
		ret = icm_field_update8(s, ICM42607_REG_INT_SOURCE0,
					ICM42607_FIFO_INT_EN,
					ICM42607_FIFO_INT_EN);
		if (ret)
			return ret;

		/* enable FIFO in streaming mode */
		ret = icm_write8(s, ICM42607_REG_FIFO_CONFIG1,
				 ICM42607_FIFO_MODE_STREAM);
		if (ret)
			return ret;
	} else {
		/* disable FIFO interrupts */
		ret = icm_field_update8(s, ICM42607_REG_INT_SOURCE0,
					ICM42607_FIFO_INT_EN, 0);
		if (ret)
			return ret;

		/* set FIFO in bypass mode */
		ret = icm_write8(s, ICM42607_REG_FIFO_CONFIG1,
				 ICM42607_FIFO_BYPASS);
		if (ret)
			return ret;

		/* flush FIFO data */
		ret = icm42607_flush_fifo(s);
		if (ret)
			return ret;
	}

	return EC_SUCCESS;
}

static int __maybe_unused icm42607_config_fifo(const struct motion_sensor_t *s,
					       int enable)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	int val;
	uint8_t old_fifo_en, fifo_en;
	int ret;

	mutex_lock(s->mutex);

	/* compute new FIFO enable bits and update FIFO config */
	fifo_en = st->fifo_en;
	if (enable)
		fifo_en |= BIT(s->type);
	else
		fifo_en &= ~BIT(s->type);

	val = ICM42607_FIFO_WM_GT_TH;
	if (fifo_en & BIT(MOTIONSENSE_TYPE_ACCEL))
		val |= ICM42607_FIFO_ACCEL_EN;
	if (fifo_en & BIT(MOTIONSENSE_TYPE_GYRO))
		val |= ICM42607_FIFO_GYRO_EN;

	ret = icm_switch_on_mclk(s);
	if (ret)
		goto out_unlock;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_FIFO_CONFIG5, val);
	if (ret)
		goto out_unlock;

	ret = icm_switch_off_mclk(s);
	if (ret)
		goto out_unlock;

	old_fifo_en = st->fifo_en;
	st->fifo_en = fifo_en;

	if (!old_fifo_en && st->fifo_en) {
		/* 1st sensor enabled => turn FIFO on */
		ret = icm42607_enable_fifo(s, 1);
		if (ret != EC_SUCCESS)
			goto out_unlock;
	} else if (old_fifo_en && !st->fifo_en) {
		/* last sensor disabled => turn FIFO off */
		ret = icm42607_enable_fifo(s, 0);
		if (ret != EC_SUCCESS)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static void __maybe_unused icm42607_push_fifo_data(struct motion_sensor_t *s,
						   const uint8_t *raw,
						   uint32_t ts)
{
	struct ec_response_motion_sensor_data vect;
	int *v = s->raw_xyz;

	if (icm42607_normalize(s, v, raw) != EC_SUCCESS)
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

static int __maybe_unused icm42607_load_fifo(struct motion_sensor_t *s,
					     uint32_t ts)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	int count, i, size;
	const uint8_t *accel, *gyro;
	int ret;

	ret = icm_read16(s, ICM42607_REG_FIFO_COUNT, &count);
	if (ret != EC_SUCCESS)
		return ret;

	if (count <= 0)
		return EC_ERROR_INVAL;

	/* flush FIFO if buffer is not large enough */
	if (count > ICM_FIFO_BUFFER) {
		CPRINTS("It should not happen, the EC is too slow for the ODR");
		/* flush FIFO data */
		ret = icm42607_flush_fifo(s);
		if (ret)
			return ret;

		return EC_ERROR_OVERFLOW;
	}

	ret = icm_read_n(s, ICM42607_REG_FIFO_DATA, st->fifo_buffer, count);
	if (ret != EC_SUCCESS)
		return ret;

	for (i = 0; i < count; i += size) {
		size = icm_fifo_decode_packet(&st->fifo_buffer[i], &accel,
					      &gyro);
		/* exit if error or FIFO is empty */
		if (size <= 0)
			return -size;
		if (accel != NULL) {
			ret = icm42607_check_sensor_stabilized(s, ts);
			if (ret == EC_SUCCESS)
				icm42607_push_fifo_data(s, accel, ts);
		}
		if (gyro != NULL) {
			ret = icm42607_check_sensor_stabilized(s + 1, ts);
			if (ret == EC_SUCCESS)
				icm42607_push_fifo_data(s + 1, gyro, ts);
		}
	}

	return EC_SUCCESS;
}

#ifdef ACCELGYRO_ICM42607_INT_ENABLE

/**
 * icm42607_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->icm42607_irq_handler().
 */
void icm42607_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ACCELGYRO_ICM42607_INT_EVENT);
}

/**
 * icm42607_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 */
static int icm42607_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int status;
	int ret;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCELGYRO_ICM42607_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	mutex_lock(s->mutex);

	/* read and clear interrupt status */
	ret = icm_read8(s, ICM42607_REG_INT_STATUS, &status);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	if (status & ICM42607_FIFO_INT_STATUS) {
		ret = icm42607_load_fifo(s, last_interrupt_timestamp);
		if (IS_ENABLED(CONFIG_ACCEL_FIFO) && (ret == EC_SUCCESS))
			motion_sense_fifo_commit_data();
	}

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm42607_config_interrupt(const struct motion_sensor_t *s)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	int val, mask;

	/* configure INT1 pin: push-pull active low */
	RETURN_ERROR(icm_write8(s, ICM42607_REG_INT_CONFIG,
				ICM42607_INT1_PUSH_PULL));

	/* configure FIFO in little endian */
	mask = ICM42607_FIFO_COUNT_ENDIAN | ICM42607_SENSOR_DATA_ENDIAN;
	RETURN_ERROR(icm_field_update8(s, ICM42607_REG_INTF_CONFIG0, mask, 0));

	RETURN_ERROR(icm_switch_on_mclk(s));

	/*
	 * configure FIFO:
	 * - enable continuous watermark interrupt
	 * - disable all FIFO en bits
	 */
	val = ICM42607_FIFO_WM_GT_TH;
	RETURN_ERROR(icm_write_mclk_reg(s, ICM42607_MREG_FIFO_CONFIG5, val));

	RETURN_ERROR(icm_switch_off_mclk(s));

	/* clear internal FIFO enable bits tracking */
	st->fifo_en = 0;

	/* set FIFO watermark to 1 data packet (8 bytes) */
	RETURN_ERROR(icm_write16(s, ICM42607_REG_FIFO_WM, 8));

	return EC_SUCCESS;
}

#endif /* ACCELGYRO_ICM42607_INT_ENABLE */

static int icm42607_enable_sensor(const struct motion_sensor_t *s, int enable)
{
	uint32_t delay, stop_delay;
	int32_t rem;
	uint8_t mask;
	int val;
	int ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		mask = ICM42607_ACCEL_MODE_MASK;
		if (enable) {
			delay = ICM42607_ACCEL_START_TIME;
			stop_delay = ICM42607_ACCEL_STOP_TIME;
			val = ICM42607_ACCEL_MODE(ICM42607_MODE_LOW_POWER);
		} else {
			delay = ICM42607_ACCEL_STOP_TIME;
			val = ICM42607_ACCEL_MODE(ICM42607_MODE_OFF);
		}
		break;
	case MOTIONSENSE_TYPE_GYRO:
		mask = ICM42607_GYRO_MODE_MASK;
		if (enable) {
			delay = ICM42607_GYRO_START_TIME;
			stop_delay = ICM42607_GYRO_STOP_TIME;
			val = ICM42607_GYRO_MODE(ICM42607_MODE_LOW_NOISE);
		} else {
			delay = ICM42607_GYRO_STOP_TIME;
			val = ICM42607_GYRO_MODE(ICM42607_MODE_OFF);
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
			usleep(rem);
	}

	mutex_lock(s->mutex);

	ret = icm_field_update8(s, ICM42607_REG_PWR_MGMT0, mask, val);
	if (ret == EC_SUCCESS) {
		icm_set_stabilize_ts(s, delay);
		/* when turning sensor on block any register write for 200 us */
		if (enable)
			usleep(200);
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int icm42607_set_data_rate(const struct motion_sensor_t *s, int rate,
				  int rnd)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);
	int reg, reg2, ret, reg_val, reg2_val;
	int normalized_rate;
	int max_rate, min_rate;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM42607_REG_ACCEL_CONFIG0;
		reg2 = ICM42607_REG_ACCEL_CONFIG1;
		min_rate = ICM42607_ACCEL_MIN_FREQ;
		max_rate = ICM42607_ACCEL_MAX_FREQ;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM42607_REG_GYRO_CONFIG0;
		reg2 = ICM42607_REG_GYRO_CONFIG1;
		min_rate = ICM42607_GYRO_MIN_FREQ;
		max_rate = ICM42607_GYRO_MAX_FREQ;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	if (rate == 0) {
		/* disable data in FIFO */
		if (IS_ENABLED(ACCELGYRO_ICM42607_INT_ENABLE))
			icm42607_config_fifo(s, 0);
		/* disable sensor */
		ret = icm42607_enable_sensor(s, 0);
		data->odr = 0;
		return ret;
	}

	/* normalize the rate */
	reg_val = ICM42607_ODR_TO_REG(rate);
	normalized_rate = ICM42607_REG_TO_ODR(reg_val);

	/* round up the rate */
	if (rnd && (normalized_rate < rate)) {
		reg_val = ICM42607_ODR_REG_UP(reg_val);
		normalized_rate = ICM42607_REG_TO_ODR(reg_val);
	}

	if (rate > 0) {
		if ((normalized_rate < min_rate) ||
		    (normalized_rate > max_rate))
			return EC_RES_INVALID_PARAM;
	}

	reg2_val = ICM42607_ODR_TO_FILT_BW(reg_val);

	mutex_lock(s->mutex);

	/* update filter bandwidth */
	ret = icm_field_update8(s, reg2, ICM42607_UI_FILT_BW_MASK,
				ICM42607_UI_FILT_BW_SET(reg2_val));
	if (ret != EC_SUCCESS)
		goto out_unlock;

	/* update ODR */
	ret = icm_field_update8(s, reg, ICM42607_ODR_MASK,
				ICM42607_ODR(reg_val));
	if (ret != EC_SUCCESS)
		goto out_unlock;

	mutex_unlock(s->mutex);

	if (data->odr == 0) {
		/* enable sensor */
		ret = icm42607_enable_sensor(s, 1);
		if (ret)
			return ret;
		/* enable data in FIFO */
		if (IS_ENABLED(ACCELGYRO_ICM42607_INT_ENABLE))
			icm42607_config_fifo(s, 1);
	}

	data->odr = normalized_rate;

	return EC_SUCCESS;

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm42607_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int reg, ret, reg_val;
	int newrange;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM42607_REG_ACCEL_CONFIG0;
		reg_val = ICM42607_ACCEL_FS_TO_REG(range);
		newrange = ICM42607_ACCEL_REG_TO_FS(reg_val);

		if (rnd && (newrange < range) && (reg_val > 0)) {
			reg_val--;
			newrange = ICM42607_ACCEL_REG_TO_FS(reg_val);
		}

		if (newrange > ICM42607_ACCEL_FS_MAX_VAL) {
			newrange = ICM42607_ACCEL_FS_MAX_VAL;
			reg_val = ICM42607_ACCEL_FS_TO_REG(range);
		}

		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM42607_REG_GYRO_CONFIG0;
		reg_val = ICM42607_GYRO_FS_TO_REG(range);
		newrange = ICM42607_GYRO_REG_TO_FS(reg_val);

		if (rnd && (newrange < range) && (reg_val > 0)) {
			reg_val--;
			newrange = ICM42607_GYRO_REG_TO_FS(reg_val);
		}

		if (newrange > ICM42607_GYRO_FS_MAX_VAL) {
			newrange = ICM42607_GYRO_FS_MAX_VAL;
			reg_val = ICM42607_GYRO_FS_TO_REG(newrange);
		}

		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	mutex_lock(s->mutex);

	ret = icm_field_update8(s, reg, ICM42607_FS_MASK,
				ICM42607_FS_SEL(reg_val));
	if (ret == EC_SUCCESS)
		s->current_range = newrange;

	mutex_unlock(s->mutex);

	return ret;
}

static int icm42607_get_hw_offset(const struct motion_sensor_t *s,
				  intv3_t offset)
{
	uint8_t raw[5];
	int i, reg, val, ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM42607_MREG_OFFSET_USER4;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM42607_MREG_OFFSET_USER0;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	mutex_lock(s->mutex);

	ret = icm_switch_on_mclk(s);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	for (i = 0; i < sizeof(raw); ++i) {
		ret = icm_read_mclk_reg(s, reg + i, &val);
		if (ret != EC_SUCCESS)
			goto out_unlock;
		raw[i] = val;
	}

	ret = icm_switch_off_mclk(s);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	mutex_unlock(s->mutex);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/*
		 * raw[0]: Accel X[11:8] | gyro Z[11:8]
		 * raw[1]: Accel X[7:0]
		 * raw[2]: Accel Y[7:0]
		 * raw[3]: Accel Z[11:8] | Accel Y[11:8]
		 * raw[4]: Accel Z[7:0]
		 */
		offset[X] = (((int)raw[0] << 4) & ~GENMASK(7, 0)) | raw[1];
		offset[Y] = (((int)raw[3] << 8) & ~GENMASK(7, 0)) | raw[2];
		offset[Z] = (((int)raw[3] << 4) & ~GENMASK(7, 0)) | raw[4];
		break;
	case MOTIONSENSE_TYPE_GYRO:
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

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm42607_set_hw_offset(const struct motion_sensor_t *s,
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

	ret = icm_switch_on_mclk(s);
	if (ret != EC_SUCCESS)
		goto out_unlock;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* Accel X[11:8] | gyro Z[11:8] */
		val = (offset[X] >> 4) & GENMASK(7, 4);
		ret = icm_field_update_mclk_reg(s, ICM42607_MREG_OFFSET_USER4,
						GENMASK(7, 4), val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel X[7:0] */
		val = offset[X] & GENMASK(7, 0);
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER5, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel Y[7:0] */
		val = offset[Y] & GENMASK(7, 0);
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER6, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel Z[11:8] | Accel Y[11:8] */
		val = ((offset[Z] >> 4) & GENMASK(7, 4)) |
		      ((offset[Y] >> 8) & GENMASK(3, 0));
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER7, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel Z[7:0] */
		val = offset[Z] & GENMASK(7, 0);
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER8, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;
		break;

	case MOTIONSENSE_TYPE_GYRO:
		/* Gyro X[7:0] */
		val = offset[X] & GENMASK(7, 0);
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER0, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Gyro Y[11:8] | Gyro X[11:8] */
		val = ((offset[Y] >> 4) & GENMASK(7, 4)) |
		      ((offset[X] >> 8) & GENMASK(3, 0));
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER1, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Gyro Y[7:0] */
		val = offset[Y] & GENMASK(7, 0);
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER2, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Gyro Z[7:0] */
		val = offset[Z] & GENMASK(7, 0);
		ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER3, val);
		if (ret != EC_SUCCESS)
			goto out_unlock;

		/* Accel X[11:8] | gyro Z[11:8] */
		val = (offset[Z] >> 8) & GENMASK(3, 0);
		ret = icm_field_update_mclk_reg(s, ICM42607_MREG_OFFSET_USER4,
						GENMASK(3, 0), val);
		if (ret != EC_SUCCESS)
			goto out_unlock;
		break;

	default:
		ret = EC_ERROR_INVAL;
		goto out_unlock;
	}

	ret = icm_switch_off_mclk(s);

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

static int icm42607_set_offset(const struct motion_sensor_t *s,
			       const int16_t *offset, int16_t temp)
{
	intv3_t v = { offset[X], offset[Y], offset[Z] };
	int div1, div2;
	int i;

	/* rotate back to chip frame */
	rotate_inv(v, *s->rot_standard_ref, v);

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

	return icm42607_set_hw_offset(s, v);
}

static int icm42607_get_offset(const struct motion_sensor_t *s, int16_t *offset,
			       int16_t *temp)
{
	intv3_t v;
	int div1, div2;
	int i, ret;

	ret = icm42607_get_hw_offset(s, v);
	if (ret != EC_SUCCESS)
		return ret;

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

static int icm42607_read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[6];
	int reg, ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		reg = ICM42607_REG_ACCEL_DATA_XYZ;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		reg = ICM42607_REG_GYRO_DATA_XYZ;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	/* read data registers if sensor is stabilized */
	mutex_lock(s->mutex);

	ret = icm42607_check_sensor_stabilized(s, __hw_clock_source_read());
	if (ret == EC_SUCCESS)
		ret = icm_read_n(s, reg, raw, sizeof(raw));

	mutex_unlock(s->mutex);
	if (ret != EC_SUCCESS)
		return ret;

	ret = icm42607_normalize(s, v, raw);
	/* if data is invalid return the previous read data */
	if (ret != EC_SUCCESS) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
	}

	return EC_SUCCESS;
}

static int icm42607_read_temp(const struct motion_sensor_t *s, int *temp_ptr)
{
	int val, ret;

	mutex_lock(s->mutex);
	ret = icm_read16(s, ICM42607_REG_TEMP_DATA, &val);
	mutex_unlock(s->mutex);

	if (ret != EC_SUCCESS)
		return ret;

	/* This register is big-endian and not configurable */
	val = be16toh(val);

	/* ensure correct propagation of 16 bits sign bit */
	val = icm_sign_extend(val, 15);

	if (val == ICM42607_INVALID_DATA)
		return EC_ERROR_NOT_POWERED;

	*temp_ptr = C_TO_K((val / 128) + 25);
	return EC_SUCCESS;
}

static int icm42607_reset_revA(const struct motion_sensor_t *s)
{
	int ret, val;

	/* clear status register */
	ret = icm_read8(s, ICM42607_REG_INT_STATUS, &val);
	if (ret)
		return ret;

	ret = icm_switch_on_mclk(s);
	if (ret)
		return ret;

	/* Set otp_copy_mode register field */
	ret = icm_field_update_mclk_reg(s, ICM42607_MREG_OTP_CONFIG,
					ICM42607_OTP_COPY_MODE_MASK,
					ICM42607_OTP_COPY_TRIM);
	if (ret)
		return ret;

	/* Set otp_power_down register field to 0 */
	ret = icm_field_update_mclk_reg(s, ICM42607_MREG_OTP_CTRL7,
					ICM42607_OTP_PWR_DOWN, 0);
	if (ret)
		return ret;

	/* Wait for 300us for the OTP to fully power up */
	usleep(300);

	/* Set otp_reload register field */
	ret = icm_field_update_mclk_reg(s, ICM42607_MREG_OTP_CTRL7,
					ICM42607_OTP_RELOAD,
					ICM42607_OTP_RELOAD);
	if (ret)
		return ret;

	/* Wait for 280 us for the OTP to load */
	usleep(280);

	/* Write POR value for all registers not loaded with OTP */
	ret = icm_write8(s, ICM42607_REG_GYRO_CONFIG0, 0x06);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_ACCEL_CONFIG0, 0x06);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_APEX_CONFIG0, 0x08);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_APEX_CONFIG1, 0x02);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_FIFO_CONFIG1, 0x01);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_FIFO_CONFIG2, 0x00);
	if (ret)
		return ret;

	ret = icm_write8(s, ICM42607_REG_FIFO_CONFIG3, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_FIFO_CONFIG5, 0x20);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_INT_SOURCE7, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_INT_SOURCE8, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_INT_SOURCE9, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_INT_SOURCE10, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG2, 0xA2);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG3, 0x85);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG4, 0x51);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG5, 0x80);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG9, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG10, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG11, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER0, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER1, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER2, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER3, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER4, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER5, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER6, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER7, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_OFFSET_USER8, 0x00);
	if (ret)
		return ret;

	ret = icm_write_mclk_reg(s, ICM42607_MREG_APEX_CONFIG12, 0x00);
	if (ret)
		return ret;

	ret = icm_switch_off_mclk(s);
	if (ret)
		return ret;

#ifdef CONFIG_ACCELGYRO_ICM_COMM_SPI
	/* Device operation in shared spi bus configuration */
	ret = icm_field_update8(s, ICM42607_REG_INTF_CONFIG0, 0x03, 0x03);
	if (ret)
		return ret;
#endif
	return EC_SUCCESS;
}

static int icm42607_reset_revB(const struct motion_sensor_t *s)
{
	int val, ret, i;

	ret = icm_write8(s, ICM42607_REG_SIGNAL_PATH_RESET,
			 ICM42607_SOFT_RESET_DEV_CONFIG);
	if (ret)
		return ret;

	/* Check reset is done, 1ms max */
	for (i = 0; i < 5; i++) {
		usleep(200);
		ret = icm_read8(s, ICM42607_REG_INT_STATUS, &val);
		if (ret)
			return ret;
		if (val == ICM42607_RESET_DONE_INT)
			break;
	}
	if (val != ICM42607_RESET_DONE_INT)
		return EC_ERROR_HW_INTERNAL;

	return EC_SUCCESS;
}

static int icm42607_init_config(const struct motion_sensor_t *s,
				const int who_am_i)
{
	int mask, val, ret;

	switch (who_am_i) {
	case ICM42607_CHIP_ICM42607P:
		ret = icm42607_reset_revA(s);
		break;
	case ICM42607_CHIP_ICM42608P:
		ret = icm42607_reset_revB(s);
		break;
	default:
		ret = EC_ERROR_ACCESS_DENIED;
		break;
	}
	if (ret)
		return ret;

	/* disable i3c support */
	mask = ICM42607_I3C_SDR_EN | ICM42607_I3C_DDR_EN;
	ret = icm_field_update8(s, ICM42607_REG_INTF_CONFIG1, mask, 0);
	if (ret)
		return ret;

	/* set averaging filter for accel, 8x is max for 400Hz */
	if (ICM42607_ACCEL_MAX_FREQ == 400000)
		val = ICM42607_UI_AVG_SET(ICM42607_UI_AVG_8X);
	else
		val = ICM42607_UI_AVG_SET(ICM42607_UI_AVG_32X);
	ret = icm_field_update8(s, ICM42607_REG_ACCEL_CONFIG1,
				ICM42607_UI_AVG_MASK, val);
	if (ret)
		return ret;

	/* disable all interrupts */
	ret = icm_write8(s, ICM42607_REG_INT_SOURCE0, 0);
	if (ret)
		return ret;

	/* disable FIFO */
	return icm_write8(s, ICM42607_REG_FIFO_CONFIG1, ICM42607_FIFO_BYPASS);
}

static int icm42607_init(struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *saved_data = ICM_GET_SAVED_DATA(s);
	int who_am_i;
	int ret, i;

	mutex_lock(s->mutex);

	/* detect chip using whoami */
	ret = icm_read8(s, ICM42607_REG_WHO_AM_I, &who_am_i);
	if (ret)
		goto out_unlock;

	if (who_am_i != ICM42607_CHIP_ICM42607P &&
	    who_am_i != ICM42607_CHIP_ICM42608P) {
		CPRINTS("Unknown WHO_AM_I: 0x%02x", who_am_i);
		ret = EC_ERROR_ACCESS_DENIED;
		goto out_unlock;
	}

	/* first time init done only for 1st sensor (accel) */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		/* configure sensor */
		ret = icm42607_init_config(s, who_am_i);
		if (ret)
			goto out_unlock;

		if (IS_ENABLED(ACCELGYRO_ICM42607_INT_ENABLE)) {
			ret = icm42607_config_interrupt(s);
			if (ret)
				goto out_unlock;
		}
	}

	for (i = X; i <= Z; i++)
		saved_data->scale[i] = MOTION_SENSE_DEFAULT_SCALE;

	saved_data->odr = 0;

	mutex_unlock(s->mutex);

	return sensor_init_done(s);

out_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

const struct accelgyro_drv icm42607_drv = {
	.init = icm42607_init,
	.read = icm42607_read,
	.read_temp = icm42607_read_temp,
	.set_range = icm42607_set_range,
	.get_resolution = icm_get_resolution,
	.set_data_rate = icm42607_set_data_rate,
	.get_data_rate = icm_get_data_rate,
	.set_offset = icm42607_set_offset,
	.get_offset = icm42607_get_offset,
	.set_scale = icm_set_scale,
	.get_scale = icm_get_scale,
#ifdef ACCELGYRO_ICM42607_INT_ENABLE
	.irq_handler = icm42607_irq_handler,
#endif
};
