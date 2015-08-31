/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Image SI1141/SI1142 light sensor driver
 *
 * Started from linux si114x driver.
 */
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_si114x.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

/**
 * Read 8bit register from device.
 */
static inline int raw_read8(const int addr, const int reg, int *data_ptr)
{
	return i2c_read8(I2C_PORT_ALS, addr, reg, data_ptr);
}

/**
 * Write 8bit register from device.
 */
static inline int raw_write8(const int addr, const int reg, int data)
{
	return i2c_write8(I2C_PORT_ALS, addr, reg, data);
}

/**
 * Read 16bit register from device.
 */
static inline int raw_read16(const int addr, const int reg, int *data_ptr)
{
	return i2c_read16(I2C_PORT_ALS, addr, reg, data_ptr);
}

/* helper function to operate on parameter values: op can be query/set/or/and */
static int si114x_param_op(const struct motion_sensor_t *s,
			   uint8_t op,
			   uint8_t param,
			   int *value)
{
	int ret;

	mutex_lock(s->mutex);

	if (op != SI114X_CMD_PARAM_QUERY) {
		ret = raw_write8(s->addr, SI114X_REG_PARAM_WR, *value);
		if (ret != EC_SUCCESS)
			goto error;
	}

	ret = raw_write8(s->addr, SI114X_REG_COMMAND, op | (param & 0x1F));
	if (ret != EC_SUCCESS)
		goto error;

	ret = raw_read8(s->addr, SI114X_REG_PARAM_RD, value);
	if (ret != EC_SUCCESS)
		goto error;

	mutex_unlock(s->mutex);

	*value &= 0xff;
	return EC_SUCCESS;
error:
	mutex_unlock(s->mutex);
	return ret;
}

static int si114x_read_results(struct motion_sensor_t *s, int nb)
{
	int i, ret, val;
	struct si114x_typed_data_t *type_data = SI114X_GET_TYPED_DATA(s);
#ifdef CONFIG_ACCEL_FIFO
	struct ec_response_motion_sensor_data vector;
#endif

	/* Read ALX result */
	for (i = 0; i < nb; i++) {
		ret = raw_read16(s->addr, type_data->base_data_reg + i * 2,
				 &val);
		if (ret)
			break;
		/* Add offset, calibration */
		if (val + type_data->offset <= 0) {
			val = 1;
		} else {
			val += type_data->offset;
			/*
			 * Proxmitiy sensor data is inverse of the distance.
			 * Return back something proportional to distance,
			 * we affine with the scale parmeter.
			 */
			if (s->type == MOTIONSENSE_TYPE_PROX)
				val = SI114X_PS_INVERSION(val);
			val = val * type_data->scale +
			      val * type_data->uscale / 10000;
		}
		s->raw_xyz[i] = val;
	}

	if (ret != EC_SUCCESS)
		return ret;

	/* Add in fifo if changed only */
	for (i = 0; i < nb; i++) {
		if (s->raw_xyz[i] != s->xyz[i])
			break;
	}
	if (i == nb)
		return EC_SUCCESS;

#ifdef CONFIG_ACCEL_FIFO
	vector.flags = 0;
	for (i = 0; i < nb; i++)
		vector.data[i] = s->raw_xyz[i];
	for (i = nb; i < 3; i++)
		vector.data[i] = 0;
	motion_sense_fifo_add_unit(&vector, s, 1);
#else
	/* We need to copy raw_xyz into xyz with mutex */
#endif
	return EC_SUCCESS;
}

void si114x_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ALS_SI114X_INT_EVENT, 0);
}

/**
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t event)
{
	int ret = EC_SUCCESS, val;
	struct si114x_drv_data_t *data = SI114X_GET_DATA(s);
	struct si114x_typed_data_t *type_data = SI114X_GET_TYPED_DATA(s);

	if (!(event & CONFIG_ALS_SI114X_INT_EVENT))
		return EC_SUCCESS;

	ret = raw_read8(s->addr, SI114X_REG_IRQ_STATUS, &val);
	if (ret < 0 || !(val & type_data->irq_flags))
		return EC_SUCCESS;

	/* clearing IRQ */
	ret = raw_write8(s->addr, SI114X_REG_IRQ_STATUS,
			 val & type_data->irq_flags);
	if (ret != EC_SUCCESS)
		CPRINTS("clearing irq failed");

	switch (data->state) {
	case SI114X_ALS_IN_PROGRESS:
	case SI114X_ALS_IN_PROGRESS_PS_PENDING:
		/* We are only reading the visible light sensor */
		ret = si114x_read_results(s, 1);
		/* Fire pending requests */
		if (data->state == SI114X_ALS_IN_PROGRESS_PS_PENDING) {
			ret = raw_write8(s->addr, SI114X_REG_COMMAND,
					SI114X_CMD_PS_FORCE);
			data->state = SI114X_PS_IN_PROGRESS;
		} else {
			data->state = SI114X_IDLE;
		}
		break;
	case SI114X_PS_IN_PROGRESS:
	case SI114X_PS_IN_PROGRESS_ALS_PENDING:
		/* Read PS results */
		ret = si114x_read_results(s, SI114X_NUM_LEDS);
		if (data->state == SI114X_PS_IN_PROGRESS_ALS_PENDING) {
			ret = raw_write8(s->addr, SI114X_REG_COMMAND,
					SI114X_CMD_ALS_FORCE);
			data->state = SI114X_ALS_IN_PROGRESS;
		} else {
			data->state = SI114X_IDLE;
		}
		break;
	case SI114X_IDLE:
	default:
		CPRINTS("Invalid state");
	}
	return ret;
}

/* Just trigger a measurement */
static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	int ret;
	uint8_t cmd;
	struct si114x_drv_data_t *data = SI114X_GET_DATA(s);

	switch (data->state) {
	case SI114X_ALS_IN_PROGRESS:
		if (s->type == MOTIONSENSE_TYPE_PROX)
			data->state = SI114X_ALS_IN_PROGRESS_PS_PENDING;
#if 0
		else
			CPRINTS("Invalid state");
#endif
		ret = EC_ERROR_BUSY;
		break;
	case SI114X_PS_IN_PROGRESS:
		if (s->type == MOTIONSENSE_TYPE_LIGHT)
			data->state = SI114X_PS_IN_PROGRESS_ALS_PENDING;
#if 0
		else
			CPRINTS("Invalid state");
#endif
		ret = EC_ERROR_BUSY;
		break;
	case SI114X_IDLE:
		switch (s->type) {
		case MOTIONSENSE_TYPE_LIGHT:
			cmd = SI114X_CMD_ALS_FORCE;
			data->state = SI114X_ALS_IN_PROGRESS;
			break;
		case MOTIONSENSE_TYPE_PROX:
			cmd = SI114X_CMD_PS_FORCE;
			data->state = SI114X_PS_IN_PROGRESS;
			break;
		default:
			CPRINTS("Invalid sensor type");
		}
		ret = raw_write8(s->addr, SI114X_REG_COMMAND, cmd);
		ret = EC_RES_IN_PROGRESS;
		break;
	case SI114X_ALS_IN_PROGRESS_PS_PENDING:
	case SI114X_PS_IN_PROGRESS_ALS_PENDING:
	default:
		ret = EC_ERROR_ACCESS_DENIED;
	}
	return ret;
}

static int si114x_set_chlist(const struct motion_sensor_t *s)
{
	int reg = 0;

	/* Not interested in temperature (AUX nor IR) */
	reg = SI114X_CHLIST_EN_ALSVIS;
	switch (SI114X_NUM_LEDS) {
	case 3:
		reg |= SI114X_CHLIST_EN_PS3;
	case 2:
		reg |= SI114X_CHLIST_EN_PS2;
	case 1:
		reg |= SI114X_CHLIST_EN_PS1;
		break;
	}

	return si114x_param_op(s, SI114X_CMD_PARAM_SET,
			SI114X_PARAM_CHLIST, &reg);
}

#ifdef CONFIG_ALS_SI114X_CHECK_REVISION
static int si114x_revisions(const struct motion_sensor_t *s)
{
	int val;
	int ret = raw_read8(s->addr, SI114X_REG_PART_ID, &val);
	if (ret != EC_SUCCESS)
		return ret;

	if (val != CONFIG_ALS_SI114X) {
		CPRINTS("invalid part");
		return EC_ERROR_ACCESS_DENIED;
	}

	ret = raw_read8(s->addr, SI114X_REG_SEQ_ID, &val);
	if (ret != EC_SUCCESS)
		return ret;

	if (val < SI114X_SEQ_REV_A03)
		CPRINTS("WARNING: old sequencer revision");

	return 0;
}
#endif

static int si114x_initialize(const struct motion_sensor_t *s)
{
	int ret, val;

	/* send reset command */
	ret = raw_write8(s->addr, SI114X_REG_COMMAND, SI114X_CMD_RESET);
	if (ret != EC_SUCCESS)
		return ret;
	msleep(20);

	/* hardware key, magic value */
	ret = raw_write8(s->addr, SI114X_REG_HW_KEY, 0x17);
	if (ret != EC_SUCCESS)
		return ret;
	msleep(20);

	/* interrupt configuration, interrupt output enable */
	ret = raw_write8(s->addr, SI114X_REG_INT_CFG, SI114X_INT_CFG_OE);
	if (ret != EC_SUCCESS)
		return ret;

	/* enable interrupt for certain activities */
	ret = raw_write8(s->addr, SI114X_REG_IRQ_ENABLE,
		SI114X_PS3_IE | SI114X_PS2_IE | SI114X_PS1_IE |
		SI114X_ALS_INT0_IE);
	if (ret != EC_SUCCESS)
		return ret;

	/* Only forced mode */
	ret = raw_write8(s->addr, SI114X_REG_MEAS_RATE, 0);
	if (ret != EC_SUCCESS)
		return ret;

	/* measure ALS every time device wakes up */
	ret = raw_write8(s->addr, SI114X_REG_ALS_RATE, 0);
	if (ret != EC_SUCCESS)
		return ret;

	/* measure proximity every time device wakes up */
	ret = raw_write8(s->addr, SI114X_REG_PS_RATE, 0);
	if (ret != EC_SUCCESS)
		return ret;

	/* set LED currents to maximum */
	switch (SI114X_NUM_LEDS) {
	case 3:
		ret = raw_write8(s->addr,
			SI114X_REG_PS_LED3, 0x0f);
		if (ret != EC_SUCCESS)
			return ret;
		ret = raw_write8(s->addr,
			SI114X_REG_PS_LED21, 0xff);
		break;
	case 2:
		ret = raw_write8(s->addr,
			SI114X_REG_PS_LED21, 0xff);
		break;
	case 1:
		ret = raw_write8(s->addr,
			SI114X_REG_PS_LED21, 0x0f);
		break;
	}
	if (ret != EC_SUCCESS)
		return ret;

	ret = si114x_set_chlist(s);
	if (ret != EC_SUCCESS)
		return ret;

	/* set normal proximity measurement mode, set high signal range
	 * PS measurement */
	val = SI114X_PARAM_PS_ADC_MISC_NORMAL_MODE;
	ret = si114x_param_op(s, SI114X_CMD_PARAM_SET,
			      SI114X_PARAM_PS_ADC_MISC, &val);
	return ret;
}

static int set_resolution(const struct motion_sensor_t *s,
				int res,
				int rnd)
{
	int ret, reg1, reg2, val;
	/* override on resolution: set the gain. between 0 to 7 */
	if (s->type == MOTIONSENSE_TYPE_PROX) {
		if (res < 0 || res > 5)
			return EC_ERROR_PARAM2;
		reg1 = SI114X_PARAM_PS_ADC_GAIN;
		reg2 = SI114X_PARAM_PS_ADC_COUNTER;
	} else {
		if (res < 0 || res > 7)
			return EC_ERROR_PARAM2;
		reg1 = SI114X_PARAM_ALSVIS_ADC_GAIN;
		reg2 = SI114X_PARAM_ALSVIS_ADC_COUNTER;
	}

	val = res;
	ret = si114x_param_op(s, SI114X_CMD_PARAM_SET, reg1, &val);
	if (ret != EC_SUCCESS)
		return ret;
	/* set recovery period to one's complement of gain */
	val = (~res & 0x07) << 4;
	ret = si114x_param_op(s, SI114X_CMD_PARAM_SET, reg2, &val);
	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	int ret, reg, val;
	if (s->type == MOTIONSENSE_TYPE_PROX)
		reg = SI114X_PARAM_PS_ADC_GAIN;
	else
		/* ignore IR led */
		reg = SI114X_PARAM_ALSVIS_ADC_GAIN;

	val = 0;
	ret = si114x_param_op(s, SI114X_CMD_PARAM_QUERY, reg, &val);
	if (ret != EC_SUCCESS)
		return -1;

	return val & 0x07;
}

static int set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	struct si114x_typed_data_t *data = SI114X_GET_TYPED_DATA(s);
	data->scale = range >> 16;
	data->uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct si114x_typed_data_t *data = SI114X_GET_TYPED_DATA(s);
	return (data->scale << 16) | (data->uscale);
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	/* Sensor in forced mode, rate is used by motion_sense */
	struct si114x_typed_data_t *data = SI114X_GET_TYPED_DATA(s);
	return data->rate;
}

static int set_data_rate(const struct motion_sensor_t *s,
				int rate,
				int rnd)
{
	struct si114x_typed_data_t *data = SI114X_GET_TYPED_DATA(s);
	data->rate = rate;
	return EC_SUCCESS;
}

static int set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	struct si114x_typed_data_t *data = SI114X_GET_TYPED_DATA(s);
	data->offset = offset[X];
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s,
			int16_t   *offset,
			int16_t    *temp)
{
	struct si114x_typed_data_t *data = SI114X_GET_TYPED_DATA(s);
	offset[X] = data->offset;
	offset[Y] = 0;
	offset[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int set_interrupt(const struct motion_sensor_t *s,
			       unsigned int threshold)
{
	/* Currently unsupported. */
	return EC_ERROR_UNKNOWN;
}

static int init(const struct motion_sensor_t *s)
{
	int ret, resol;

	/* initialize only once: light must be declared first. */
	if (s->type == MOTIONSENSE_TYPE_LIGHT) {
#ifdef CONFIG_ALS_SI114X_CHECK_REVISION
		ret = si114x_revisions(s);
		if (ret != EC_SUCCESS)
			return ret;
#endif
		ret = si114x_initialize(s);
		if (ret != EC_SUCCESS)
			return ret;
		resol = 7;
	} else {
		resol = 5;
	}

	set_range(s, s->default_range, 0);
	/*
	 * Sensor is most likely behind a glass.
	 * Max out the gain to get correct measurement
	 */
	set_resolution(s, resol, 0);

	CPRINTF("[%T %s: MS Done Init type:0x%X range:%d]\n",
			s->name, s->type, get_range(s));
	return EC_SUCCESS;
}

const struct accelgyro_drv si114x_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = NULL,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.set_interrupt = set_interrupt,
	.irq_handler = irq_handler,
#endif
	.load_fifo = NULL,
};

struct si114x_drv_data_t g_si114x_data = {
	.state = SI114X_IDLE,
	.type_data = {
		/* Proximity */
		{
			.base_data_reg = SI114X_REG_PS1_DATA0,
			.irq_flags = SI114X_PS_INT_FLAG,
			.scale = 1,
			.offset = -256,
		},
		/* light */
		{
			.base_data_reg = SI114X_REG_ALSVIS_DATA0,
			.irq_flags = SI114X_ALS_INT_FLAG,
			.scale = 1,
			.offset = -256,
		}
	}
};
