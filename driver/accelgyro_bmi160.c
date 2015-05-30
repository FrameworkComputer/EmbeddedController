/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI160/BMC50 accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/mag_bmm150.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg_val; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
static const struct accel_param_pair g_ranges[] = {
	{2, BMI160_GSEL_2G},
	{4, BMI160_GSEL_4G},
	{8, BMI160_GSEL_8G},
	{16, BMI160_GSEL_16G}
};

/*
 * List of angular rate range values in +/-dps's
 * and their associated register values.
 */
const struct accel_param_pair dps_ranges[] = {
	{125, BMI160_DPS_SEL_125},
	{250, BMI160_DPS_SEL_250},
	{500, BMI160_DPS_SEL_500},
	{1000, BMI160_DPS_SEL_1000},
	{2000, BMI160_DPS_SEL_2000}
};

static inline const struct accel_param_pair *get_range_table(
		enum motionsensor_type type, int *psize)
{
	if (MOTIONSENSE_TYPE_ACCEL == type) {
		if (psize)
			*psize = ARRAY_SIZE(g_ranges);
		return g_ranges;
	} else {
		if (psize)
			*psize = ARRAY_SIZE(dps_ranges);
		return dps_ranges;
	}
}

static inline int get_xyz_reg(enum motionsensor_type type)
{
	switch (type) {
	case MOTIONSENSE_TYPE_ACCEL:
		return BMI160_ACC_X_L_G;
	case MOTIONSENSE_TYPE_GYRO:
		return BMI160_GYR_X_L_G;
	case MOTIONSENSE_TYPE_MAG:
		return BMI160_MAG_X_L_G;
	default:
		return -1;
	}
}

/**
 * @return reg value that matches the given engineering value passed in.
 * The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid reg value. If the request is
 * outside the range of values, it returns the closest valid reg value.
 */
static int get_reg_val(const int eng_val, const int round_up,
		const struct accel_param_pair *pairs, const int size)
{
	int i;
	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			break;

		if (eng_val < pairs[i+1].val) {
			if (round_up)
				i += 1;
			break;
		}
	}
	return pairs[i].reg_val;
}

/**
 * @return engineering value that matches the given reg val
 */
static int get_engineering_val(const int reg_val,
		const struct accel_param_pair *pairs, const int size)
{
	int i;
	for (i = 0; i < size; i++) {
		if (reg_val == pairs[i].reg_val)
			break;
	}
	return pairs[i].val;
}

/**
 * Read 8bit register from accelerometer.
 */
static inline int raw_read8(const int addr, const int reg, int *data_ptr)
{
	return i2c_read8(I2C_PORT_ACCEL, addr, reg, data_ptr);
}

/**
 * Write 8bit register from accelerometer.
 */
static inline int raw_write8(const int addr, const int reg, int data)
{
	return i2c_write8(I2C_PORT_ACCEL, addr, reg, data);
}

/**
 * Read 16bit register from accelerometer.
 */
static inline int raw_read16(const int addr, const int reg, int *data_ptr)
{
	return i2c_read16(I2C_PORT_ACCEL, addr, reg, data_ptr);
}

/**
 * Read 32bit register from accelerometer.
 */
static inline int raw_read32(const int addr, const int reg, int *data_ptr)
{
	return i2c_read32(I2C_PORT_ACCEL, addr, reg, data_ptr);
}

#ifdef CONFIG_MAG_BMI160_BMM150
/**
 * Control access to the compass on the secondary i2c interface:
 * enable values are:
 * 1: manual access, we can issue i2c to the compass
 * 0: data access: BMI160 gather data periodically from the compass.
 */
static int bmm150_mag_access_ctrl(const int addr, const int enable)
{
	int mag_if_ctrl;
	raw_read8(addr, BMI160_MAG_IF_1, &mag_if_ctrl);
	if (enable) {
		mag_if_ctrl |= BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_1;
	} else {
		mag_if_ctrl &= ~BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_8;
	}
	return raw_write8(addr, BMI160_MAG_IF_1, mag_if_ctrl);
}

/**
 * Read register from compass.
 * Assuming we are in manual access mode, read compass i2c register.
 */
static int raw_mag_read8(const int addr, const int reg, int *data_ptr)
{
	/* Only read 1 bytes */
	raw_write8(addr, BMI160_MAG_I2C_READ_ADDR, reg);
	return raw_read8(addr, BMI160_MAG_I2C_READ_DATA, data_ptr);
}

/**
 * Write register from compass.
 * Assuming we are in manual access mode, write to compass i2c register.
 */
static int raw_mag_write8(const int addr, const int reg, int data)
{
	raw_write8(addr, BMI160_MAG_I2C_WRITE_DATA, data);
	return raw_write8(addr, BMI160_MAG_I2C_WRITE_ADDR, reg);
}
#endif

static int set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	int ret, range_tbl_size;
	uint8_t reg_val, ctrl_reg;
	const struct accel_param_pair *ranges;
	struct motion_data_t *data = BMI160_GET_SAVED_DATA(s);

	if (s->type == MOTIONSENSE_TYPE_MAG) {
		data->range = range;
		return EC_SUCCESS;
	}

	ctrl_reg = BMI160_RANGE_REG(s->type);
	ranges = get_range_table(s->type, &range_tbl_size);
	reg_val = get_reg_val(range, rnd, ranges, range_tbl_size);

	ret = raw_write8(s->i2c_addr, ctrl_reg, reg_val);
	/* Now that we have set the range, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->range = get_engineering_val(reg_val, ranges,
				range_tbl_size);
	return ret;
}

static int get_range(const struct motion_sensor_t *s,
				int *range)
{
	struct motion_data_t *data = BMI160_GET_SAVED_DATA(s);

	*range = data->range;
	return EC_SUCCESS;
}

static int set_resolution(const struct motion_sensor_t *s,
				int res,
				int rnd)
{
	/* Only one resolution, BMI160_RESOLUTION, so nothing to do. */
	return EC_SUCCESS;
}

static int get_resolution(const struct motion_sensor_t *s,
				int *res)
{
	*res = BMI160_RESOLUTION;
	return EC_SUCCESS;
}

static int set_data_rate(const struct motion_sensor_t *s,
				int rate,
				int rnd)
{
	int ret, val, normalized_rate;
	uint8_t ctrl_reg, reg_val;
	struct motion_data_t *data = BMI160_GET_SAVED_DATA(s);

	if (rate == 0) {
		/* go to suspend mode */
		ret = raw_write8(s->i2c_addr, BMI160_CMD_REG,
				 BMI160_CMD_MODE_SUSPEND(s->type));
		msleep(3);
		data->odr = 0;
		return ret;
	} else if (data->odr == 0) {
		/* back from suspend mode */
		ret = raw_write8(s->i2c_addr, BMI160_CMD_REG,
				 BMI160_CMD_MODE_NORMAL(s->type));
		msleep(3);
	}
	ctrl_reg = BMI160_CONF_REG(s->type);
	reg_val = BMI160_ODR_TO_REG(rate);
	normalized_rate = BMI160_REG_TO_ODR(reg_val);
	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate *= 2;
	}

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		if (reg_val > BMI160_ODR_1600HZ) {
			reg_val = BMI160_ODR_1600HZ;
			normalized_rate = 1600000;
		} else if (reg_val < BMI160_ODR_0_78HZ) {
			reg_val = BMI160_ODR_0_78HZ;
			normalized_rate = 780;
		}
		break;
	case MOTIONSENSE_TYPE_GYRO:
		if (reg_val > BMI160_ODR_3200HZ) {
			reg_val = BMI160_ODR_3200HZ;
			normalized_rate = 3200000;
		} else if (reg_val < BMI160_ODR_25HZ) {
			reg_val = BMI160_ODR_25HZ;
			normalized_rate = 25000;
		}
		break;
	case MOTIONSENSE_TYPE_MAG:
		if (reg_val > BMI160_ODR_800HZ) {
			reg_val = BMI160_ODR_800HZ;
			normalized_rate = 800000;
		} else if (reg_val < BMI160_ODR_0_78HZ) {
			reg_val = BMI160_ODR_0_78HZ;
			normalized_rate = 780;
		}
		break;

	default:
		return -1;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->i2c_addr, ctrl_reg, &val);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	val = (val & ~BMI160_ODR_MASK) | reg_val;
	ret = raw_write8(s->i2c_addr, ctrl_reg, val);

	/* Now that we have set the odr, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->odr = normalized_rate;

accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s,
				int *rate)
{
	struct motion_data_t *data = BMI160_GET_SAVED_DATA(s);

	*rate = data->odr;
	return EC_SUCCESS;
}

void normalize(const struct motion_sensor_t *s, vector_3_t v, uint8_t *data)
{
	int range;

	v[0] = ((int16_t)((data[1] << 8) | data[0]));
	v[1] = ((int16_t)((data[3] << 8) | data[2]));
	v[2] = ((int16_t)((data[5] << 8) | data[4]));

	get_range(s, &range);

	v[0] *= range;
	v[1] *= range;
	v[2] *= range;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* normalize the accel scale: 1G = 1024 */
		v[0] >>= 5;
		v[1] >>= 5;
		v[2] >>= 5;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		v[0] >>= 8;
		v[1] >>= 8;
		v[2] >>= 8;
		break;
	default:
		break;
	}
}

#ifdef CONFIG_ACCEL_INTERRUPTS
/**
 * bmi160_interrupt - called when the sensor activate the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->irq_handler().
 */
void bmi160_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_MOTIONSENSE, TASK_EVENT_MOTION_INTERRUPT, 0);
}


static int set_interrupt(const struct motion_sensor_t *s,
			       unsigned int threshold)
{
	int ret, tmp;
	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	mutex_lock(s->mutex);
	raw_write8(s->i2c_addr, BMI160_CMD_REG, BMI160_CMD_FIFO_FLUSH);
	msleep(30);
	raw_write8(s->i2c_addr, BMI160_CMD_REG, BMI160_CMD_INT_RESET);

	/* Latch until interupts */
	/* configure int2 as an external input */
	tmp = BMI160_INT2_INPUT_EN | BMI160_LATCH_FOREVER;
	ret = raw_write8(s->i2c_addr, BMI160_INT_LATCH, tmp);

	/* configure int1 as an interupt */
	ret = raw_write8(s->i2c_addr, BMI160_INT_OUT_CTRL,
		BMI160_INT_CTRL(1, OUTPUT_EN));

	/* Map Simple/Double Tap to int 1
	 * Map Flat interrupt to int 1
	 */
	ret = raw_write8(s->i2c_addr, BMI160_INT_MAP_REG(1),
		BMI160_INT_FLAT | BMI160_INT_D_TAP | BMI160_INT_S_TAP);

#ifdef CONFIG_ACCEL_FIFO
	/* map fifo water mark to int 1 */
	ret = raw_write8(s->i2c_addr, BMI160_INT_FIFO_MAP,
			BMI160_INT_MAP(1, FWM));

	/* configure fifo watermark at 50% */
	ret = raw_write8(s->i2c_addr, BMI160_FIFO_CONFIG_0,
			512 / sizeof(uint32_t));
	ret = raw_write8(s->i2c_addr, BMI160_FIFO_CONFIG_1,
			BMI160_FIFO_TAG_INT1_EN |
			BMI160_FIFO_TAG_INT2_EN |
			BMI160_FIFO_HEADER_EN |
			BMI160_FIFO_MAG_EN |
			BMI160_FIFO_ACC_EN |
			BMI160_FIFO_GYR_EN);
#endif

	/* Set double tap interrupt and fifo*/
	ret = raw_read8(s->i2c_addr, BMI160_INT_EN_0, &tmp);
	tmp |= BMI160_INT_FLAT_EN | BMI160_INT_D_TAP_EN | BMI160_INT_S_TAP_EN;
	ret = raw_write8(s->i2c_addr, BMI160_INT_EN_0, tmp);

#ifdef CONFIG_ACCEL_FIFO
	ret = raw_read8(s->i2c_addr, BMI160_INT_EN_1, &tmp);
	tmp |= BMI160_INT_FWM_EN;
	ret = raw_write8(s->i2c_addr, BMI160_INT_EN_1, tmp);
#endif

	mutex_unlock(s->mutex);
	return ret;
}

/**
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
int irq_handler(const struct motion_sensor_t *s)
{
	int interrupt;

	raw_read32(s->i2c_addr, BMI160_INT_STATUS_0, &interrupt);
	raw_write8(s->i2c_addr, BMI160_CMD_REG, BMI160_CMD_INT_RESET);

	if (interrupt & BMI160_S_TAP_INT)
		CPRINTS("single tap: %08x", interrupt);
	if (interrupt & BMI160_D_TAP_INT)
		CPRINTS("double tap: %08x", interrupt);
	if (interrupt & BMI160_FLAT_INT)
		CPRINTS("flat: %08x", interrupt);
	/*
	 * No need to read the FIFO here, motion sense task is
	 * doing it on every interrupt.
	 */
	return EC_SUCCESS;
}

#endif  /* CONFIG_ACCEL_INTERRUPTS */

#ifdef CONFIG_ACCEL_FIFO
enum fifo_state {
	FIFO_HEADER,
	FIFO_DATA_SKIP,
	FIFO_DATA_TIME,
	FIFO_DATA_CONFIG,
};


#define BMI160_FIFO_BUFFER 64
static uint8_t bmi160_buffer[BMI160_FIFO_BUFFER];
#define BUFFER_END(_buffer) ((_buffer) + sizeof(_buffer))
/*
 * Decode the header from the fifo.
 * Return 0 if we need further processing.
 * Sensor mutex must be held during processing, to protect the fifos.
 *
 * @s: base sensor
 * @hdr: the header to decode
 * @bp: current pointer in the buffer, updated when processing the header.
 */
static int bmi160_decode_header(struct motion_sensor_t *s,
		enum fifo_header hdr, uint8_t **bp)
{
	if ((hdr & BMI160_FH_MODE_MASK) == BMI160_EMPTY &&
			(hdr & BMI160_FH_PARM_MASK) != 0) {
		int i, size = 0;
		/* Check if there is enough space for the data frame */
		for (i = MOTIONSENSE_TYPE_MAG; i >= MOTIONSENSE_TYPE_ACCEL;
		     i--) {
			if (hdr & (1 << (i + BMI160_FH_PARM_OFFSET)))
				size += (i == MOTIONSENSE_TYPE_MAG ? 8 : 6);
		}
		if (*bp + size > BUFFER_END(bmi160_buffer)) {
			/* frame is not complete, it
			 * will be retransmitted.
			 */
			*bp = BUFFER_END(bmi160_buffer);
			return 1;
		}
		for (i = MOTIONSENSE_TYPE_MAG; i >= MOTIONSENSE_TYPE_ACCEL;
		     i--) {
			if (hdr & (1 << (i + BMI160_FH_PARM_OFFSET))) {
				struct ec_response_motion_sensor_data vector;
				int *v = (s + i)->raw_xyz;
				vector.flags = 0;
				normalize(s + i, v, *bp);
				vector.data[X] = v[X];
				vector.data[Y] = v[Y];
				vector.data[Z] = v[Z];
				motion_sense_fifo_add_unit(&vector, s + i);
				*bp += (i == MOTIONSENSE_TYPE_MAG ? 8 : 6);
			}
		}
#if 0
		if (hdr & BMI160_FH_EXT_MASK)
			CPRINTF("%s%s\n",
				(hdr & 0x1 ? "INT1" : ""),
				(hdr & 0x2 ? "INT2" : ""));
#endif
		return 1;
	} else {
		return 0;
	}
}

static int load_fifo(struct motion_sensor_t *s)
{
	int done = 0;
	int fifo_length;

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	/* Read fifo length */
	raw_read16(s->i2c_addr, BMI160_FIFO_LENGTH_0, &fifo_length);
	fifo_length &= BMI160_FIFO_LENGTH_MASK;
	if (fifo_length == 0)
		return EC_SUCCESS;
	do {
		enum fifo_state state = FIFO_HEADER;
		uint8_t fifo_reg = BMI160_FIFO_DATA;
		uint8_t *bp = bmi160_buffer;
		i2c_lock(I2C_PORT_ACCEL, 1);
		i2c_xfer(I2C_PORT_ACCEL, s->i2c_addr,
				&fifo_reg, 1, bmi160_buffer,
				sizeof(bmi160_buffer), I2C_XFER_SINGLE);
		i2c_lock(I2C_PORT_ACCEL, 0);
		while (!done && bp != BUFFER_END(bmi160_buffer)) {
			switch (state) {
			case FIFO_HEADER: {
				enum fifo_header hdr = *bp++;
				if (bmi160_decode_header(s, hdr, &bp))
					continue;
				/* Other cases */
				hdr &= 0xdc;
				switch (hdr) {
				case BMI160_EMPTY:
					done = 1;
					break;
				case BMI160_SKIP:
					state = FIFO_DATA_SKIP;
					break;
				case BMI160_TIME:
					state = FIFO_DATA_TIME;
					break;
				case BMI160_CONFIG:
					state = FIFO_DATA_CONFIG;
					break;
				default:
					CPRINTS("Unknown header: 0x%02x", hdr);
				}
				break;
			}
			case FIFO_DATA_SKIP:
				CPRINTF("skipped %d frames\n", *bp++);
				state = FIFO_HEADER;
				break;
			case FIFO_DATA_CONFIG:
				CPRINTF("config change: 0x%02x\n", *bp++);
				state = FIFO_HEADER;
				break;
			case FIFO_DATA_TIME:
				if (bp + 3 > BUFFER_END(bmi160_buffer)) {
					bp = BUFFER_END(bmi160_buffer);
					continue;
				}
				/* We are not requesting timestamp */
				CPRINTF("timestamp %d\n", (bp[2] << 16) |
					(bp[1] << 8) | bp[0]);
				state = FIFO_HEADER;
				bp += 3;
				break;
			default:
				CPRINTS("Unknown data: 0x%02x\n", *bp++);
				state = FIFO_HEADER;
			}
		}
	} while (!done);
	return EC_SUCCESS;
}
#endif  /* CONFIG_ACCEL_FIFO */


static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = raw_read8(s->i2c_addr, BMI160_STATUS, &tmp);

	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RS Error]", s->name, s->type);
		return ret;
	}

	*ready = tmp & BMI160_DRDY_MASK(s->type);
	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t data[6];
	uint8_t xyz_reg;
	int ret, tmp = 0;

	ret = is_data_ready(s, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that motion senor task can read again
	 * to get the latest updated sensor data quickly.
	 */
	if (!tmp) {
		v[0] = s->raw_xyz[0];
		v[1] = s->raw_xyz[1];
		v[2] = s->raw_xyz[2];
		return EC_SUCCESS;
	}

	xyz_reg = get_xyz_reg(s->type);

	/* Read 6 bytes starting at xyz_reg */
	i2c_lock(I2C_PORT_ACCEL, 1);
	ret = i2c_xfer(I2C_PORT_ACCEL, s->i2c_addr,
			&xyz_reg, 1, data, 6, I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_ACCEL, 0);

	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RD XYZ Error %d]",
			s->name, s->type, ret);
		return ret;
	}
	normalize(s, v, data);
	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;

	ret = raw_read8(s->i2c_addr, BMI160_CHIP_ID, &tmp);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (tmp != BMI160_CHIP_ID_MAJOR)
		return EC_ERROR_ACCESS_DENIED;


	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);

		/* Reset the chip to be in a good state */
		raw_write8(s->i2c_addr, BMI160_CMD_REG,
				BMI160_CMD_SOFT_RESET);
		msleep(30);
		data->flags &= ~BMI160_FLAG_SEC_I2C_ENABLED;
		/* To avoid gyro wakeup */
		raw_write8(s->i2c_addr, BMI160_PMU_TRIGGER, 0);
	}

	raw_write8(s->i2c_addr, BMI160_CMD_REG,
			BMI160_CMD_MODE_NORMAL(s->type));
	msleep(30);

	set_range(s, s->runtime_config.range, 0);
	set_data_rate(s, s->runtime_config.odr, 0);

#ifdef CONFIG_MAG_BMI160_BMM150
	if (s->type == MOTIONSENSE_TYPE_MAG) {
		struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);
		if ((data->flags & BMI160_FLAG_SEC_I2C_ENABLED) == 0) {
			int ext_page_reg, pullup_reg;
			/* Enable secondary interface */
			/*
			 * This is not part of the normal configuration but from
			 * code on Bosh github repo:
			 * https://github.com/BoschSensortec/BMI160_driver
			 *
			 * Magic command sequences
			 */
			raw_write8(s->i2c_addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B0);
			raw_write8(s->i2c_addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B1);
			raw_write8(s->i2c_addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B2);

			/*
			 * Change the register page to target mode, to change
			 * the internal pull ups of the secondary interface.
			 */
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg | BMI160_CMD_TARGET_PAGE);
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg | BMI160_CMD_PAGING_EN);
			raw_read8(s->i2c_addr, BMI160_COM_C_TRIM_ADDR,
					&pullup_reg);
			raw_write8(s->i2c_addr, BMI160_COM_C_TRIM_ADDR,
					pullup_reg | BMI160_COM_C_TRIM);
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg & ~BMI160_CMD_TARGET_PAGE);
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);

			/* Set the i2c address of the compass */
			ret = raw_write8(s->i2c_addr, BMI160_MAG_IF_0,
					BMM150_I2C_ADDRESS);

			/* Enable the secondary interface as I2C */
			ret = raw_write8(s->i2c_addr, BMI160_IF_CONF,
				BMI160_IF_MODE_AUTO_I2C << BMI160_IF_MODE_OFF);
			data->flags |= BMI160_FLAG_SEC_I2C_ENABLED;
		}


		bmm150_mag_access_ctrl(s->i2c_addr, 1);
		/* Set the compass from Suspend to Sleep */
		ret = raw_mag_write8(s->i2c_addr, BMM150_PWR_CTRL,
				BMM150_PWR_ON);
		/* Now we can read the device id */
		ret = raw_mag_read8(s->i2c_addr, BMM150_CHIP_ID, &tmp);
		if (ret)
			return EC_ERROR_UNKNOWN;

		if (tmp != BMM150_CHIP_ID_MAJOR)
			return EC_ERROR_ACCESS_DENIED;

		/* Leave the address for reading the data */
		raw_write8(s->i2c_addr, BMI160_MAG_I2C_READ_ADDR,
				BMM150_BASE_DATA);
		/*
		 * Set the compass forced mode, to sleep after each measure.
		 */
		ret = raw_mag_write8(s->i2c_addr, BMM150_OP_CTRL,
			BMM150_OP_MODE_FORCED << BMM150_OP_MODE_OFFSET);

		/*
		 * Put back the secondary interface in normal mode.
		 * BMI160 will poll based on the configure ODR.
		 */
		bmm150_mag_access_ctrl(s->i2c_addr, 0);
	}
#endif
#ifdef CONFIG_ACCEL_INTERRUPTS
	ret = s->drv->set_interrupt(s, 0);
#endif
	/* Fifo setup is done elsewhere */
	CPRINTF("[%T %s: MS Done Init type:0x%X range:%d odr:%d]\n",
			s->name, s->type, s->runtime_config.range,
			s->runtime_config.odr);
	return ret;
}

const struct accelgyro_drv bmi160_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.set_interrupt = set_interrupt,
	.irq_handler = irq_handler,
#endif
#ifdef CONFIG_ACCEL_FIFO
	.load_fifo = load_fifo,
#endif
};

struct bmi160_drv_data_t g_bmi160_data = {
	.flags = 0,
};
