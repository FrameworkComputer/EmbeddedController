/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "accelgyro_bmi_common.h"
#include "console.h"
#include "i2c.h"
#include "mag_bmm150.h"
#include "mag_lis2mdl.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "spi.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

#if !defined(CONFIG_ACCELGYRO_BMI160) && !defined(CONFIG_ACCELGYRO_BMI220) && \
	!defined(CONFIG_ACCELGYRO_BMI260) && !defined(CONFIG_ACCELGYRO_BMI3XX)
#error "Must use following sensors BMI160 BMI220 BMI260 BMI3XX"
#endif

#if (defined(CONFIG_ACCELGYRO_BMI260) || defined(CONFIG_ACCELGYRO_BMI220)) && \
	!defined(CONFIG_ACCELGYRO_BMI160)
#define V(s_) 1
#elif defined(CONFIG_ACCELGYRO_BMI160) &&     \
	!(defined(CONFIG_ACCELGYRO_BMI260) || \
	  defined(CONFIG_ACCELGYRO_BMI220))
#define V(s_) 0
#else
#define V(s_)                                     \
	((s_)->chip == MOTIONSENSE_CHIP_BMI260 || \
	 (s_)->chip == MOTIONSENSE_CHIP_BMI220)
#endif
/* Index for which table to use. */
#if defined(CONFIG_ACCELGYRO_BMI160) && \
	(defined(CONFIG_ACCELGYRO_BMI220) || defined(CONFIG_ACCELGYRO_BMI260))
#define T(s_) V(s_)
#else
#define T(s_) 0
#endif

/* List of range values in +/-G's and their associated register values. */
const struct bmi_accel_param_pair g_ranges[][4] = {
#ifdef CONFIG_ACCELGYRO_BMI160
	{ { 2, BMI160_GSEL_2G },
	  { 4, BMI160_GSEL_4G },
	  { 8, BMI160_GSEL_8G },
	  { 16, BMI160_GSEL_16G } },
#endif
#if defined(CONFIG_ACCELGYRO_BMI220) || defined(CONFIG_ACCELGYRO_BMI260)
	{ { 2, BMI260_GSEL_2G },
	  { 4, BMI260_GSEL_4G },
	  { 8, BMI260_GSEL_8G },
	  { 16, BMI260_GSEL_16G } },
#endif
};

/*
 * List of angular rate range values in +/-dps's
 * and their associated register values.
 */
const struct bmi_accel_param_pair dps_ranges[][5] = {
#ifdef CONFIG_ACCELGYRO_BMI160
	{ { 125, BMI160_DPS_SEL_125 },
	  { 250, BMI160_DPS_SEL_250 },
	  { 500, BMI160_DPS_SEL_500 },
	  { 1000, BMI160_DPS_SEL_1000 },
	  { 2000, BMI160_DPS_SEL_2000 } },
#endif
#if defined(CONFIG_ACCELGYRO_BMI220) || defined(CONFIG_ACCELGYRO_BMI260)
	{ { 125, BMI260_DPS_SEL_125 },
	  { 250, BMI260_DPS_SEL_250 },
	  { 500, BMI260_DPS_SEL_500 },
	  { 1000, BMI260_DPS_SEL_1000 },
	  { 2000, BMI260_DPS_SEL_2000 } },
#endif
};

int bmi_get_xyz_reg(const struct motion_sensor_t *s)
{
	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		return BMI_ACC_DATA(V(s));
	case MOTIONSENSE_TYPE_GYRO:
		return BMI_GYR_DATA(V(s));
	case MOTIONSENSE_TYPE_MAG:
		return BMI_AUX_DATA(V(s));
	default:
		return -1;
	}
}

const struct bmi_accel_param_pair *
bmi_get_range_table(const struct motion_sensor_t *s, int *psize)
{
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		if (psize)
			*psize = ARRAY_SIZE(g_ranges[T(s)]);
		return g_ranges[T(s)];
	}
	if (psize)
		*psize = ARRAY_SIZE(dps_ranges[T(s)]);
	return dps_ranges[T(s)];
}

/**
 * @return reg value that matches the given engineering value passed in.
 * The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid reg value. If the request is
 * outside the range of values, it returns the closest valid reg value.
 */
int bmi_get_reg_val(const int eng_val, const int round_up,
		    const struct bmi_accel_param_pair *pairs, const int size)
{
	int i;

	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			break;

		if (eng_val < pairs[i + 1].val) {
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
int bmi_get_engineering_val(const int reg_val,
			    const struct bmi_accel_param_pair *pairs,
			    const int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (reg_val == pairs[i].reg_val)
			break;
	}
	return pairs[i].val;
}

#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
static int bmi_spi_raw_read(const int addr, const uint8_t reg, uint8_t *data,
			    const int len)
{
	uint8_t cmd = 0x80 | reg;

	return spi_transaction(&spi_devices[addr], &cmd, 1, data, len);
}
#endif

/**
 * Read 8bit register from accelerometer.
 */
int bmi_read8(const int port, const uint16_t i2c_spi_addr_flags, const int reg,
	      int *data_ptr)
{
	int rv;

#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	{
		uint8_t val;

		rv = bmi_spi_raw_read(ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags),
				      reg, &val, 1);
		if (rv == EC_SUCCESS)
			*data_ptr = val;
	}
#else
	rv = i2c_read8(port, i2c_spi_addr_flags, reg, data_ptr);
#endif
	return rv;
}

/**
 * Write 8bit register from accelerometer.
 */
int bmi_write8(const int port, const uint16_t i2c_spi_addr_flags, const int reg,
	       int data)
{
	int rv;

#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	{
		uint8_t cmd[2] = { reg, data };

		rv = spi_transaction(
			&spi_devices[ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags)],
			cmd, 2, NULL, 0);
	}
#else
	rv = i2c_write8(port, i2c_spi_addr_flags, reg, data);
#endif
	/*
	 * From Bosch:  BMI needs a delay of 450us after each write if it
	 * is in suspend mode, otherwise the operation may be ignored by
	 * the sensor. Given we are only doing write during init, add
	 * the delay unconditionally.
	 */
	crec_msleep(1);

	return rv;
}

/**
 * Read 16bit register from accelerometer.
 */
int bmi_read16(const int port, const uint16_t i2c_spi_addr_flags,
	       const uint8_t reg, int *data_ptr)
{
#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	return bmi_spi_raw_read(ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags), reg,
				(uint8_t *)data_ptr, 2);
#else
	return i2c_read16(port, i2c_spi_addr_flags, reg, data_ptr);
#endif
}

/**
 * Write 16bit register from accelerometer.
 */
int bmi_write16(const int port, const uint16_t i2c_spi_addr_flags,
		const int reg, int data)
{
	int rv = -EC_ERROR_PARAM1;

#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	CPRINTS("%s() spi part is not implemented", __func__);
#else
	rv = i2c_write16(port, i2c_spi_addr_flags, reg, data);
#endif
	/*
	 * From Bosch:  BMI needs a delay of 450us after each write if it
	 * is in suspend mode, otherwise the operation may be ignored by
	 * the sensor. Given we are only doing write during init, add
	 * the delay unconditionally.
	 */
	crec_msleep(1);
	return rv;
}

/**
 * Read 32bit register from accelerometer.
 */
int bmi_read32(const int port, const uint16_t i2c_spi_addr_flags,
	       const uint8_t reg, int *data_ptr)
{
#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	return bmi_spi_raw_read(ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags), reg,
				(uint8_t *)data_ptr, 4);
#else
	return i2c_read32(port, i2c_spi_addr_flags, reg, data_ptr);
#endif
}

/**
 * Read n bytes from accelerometer.
 */
int bmi_read_n(const int port, const uint16_t i2c_spi_addr_flags,
	       const uint8_t reg, uint8_t *data_ptr, const int len)
{
#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	return bmi_spi_raw_read(ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags), reg,
				data_ptr, len);
#else
	return i2c_read_block(port, i2c_spi_addr_flags, reg, data_ptr, len);
#endif
}

/**
 * Write n bytes from accelerometer.
 */
int bmi_write_n(const int port, const uint16_t i2c_spi_addr_flags,
		const uint8_t reg, const uint8_t *data_ptr, const int len)
{
	int rv = -EC_ERROR_PARAM1;

#ifdef CONFIG_ACCELGYRO_BMI_COMM_SPI
	CPRINTS("%s() spi part is not implemented", __func__);
#else
	rv = i2c_write_block(port, i2c_spi_addr_flags, reg, data_ptr, len);
#endif
	/*
	 * From Bosch:  BMI needs a delay of 450us after each write if it
	 * is in suspend mode, otherwise the operation may be ignored by
	 * the sensor. Given we are only doing write during init, add
	 * the delay unconditionally.
	 */
	crec_msleep(1);

	return rv;
}
/*
 * Enable/Disable specific bit set of a 8-bit reg.
 */
int bmi_enable_reg8(const struct motion_sensor_t *s, int reg, uint8_t bits,
		    int enable)
{
	if (enable)
		return bmi_set_reg8(s, reg, bits, 0);
	return bmi_set_reg8(s, reg, 0, bits);
}

/*
 * Set specific bit set to certain value of a 8-bit reg.
 */
int bmi_set_reg8(const struct motion_sensor_t *s, int reg, uint8_t bits,
		 int mask)
{
	int ret, val;

	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, reg, &val);
	if (ret)
		return ret;
	val = (val & ~mask) | bits;
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, reg, val);
	return ret;
}

void bmi_normalize(const struct motion_sensor_t *s, intv3_t v, uint8_t *input)
{
	int i;
	struct accelgyro_saved_data_t *data = BMI_GET_SAVED_DATA(s);

	if (IS_ENABLED(CONFIG_MAG_BMI_BMM150) &&
	    (s->type == MOTIONSENSE_TYPE_MAG)) {
		bmm150_normalize(s, v, input);
	} else if (IS_ENABLED(CONFIG_MAG_BMI_LIS2MDL) &&
		   (s->type == MOTIONSENSE_TYPE_MAG)) {
		lis2mdl_normalize(s, v, input);
	} else {
		v[0] = ((int16_t)((input[1] << 8) | input[0]));
		v[1] = ((int16_t)((input[3] << 8) | input[2]));
		v[2] = ((int16_t)((input[5] << 8) | input[4]));
	}
	rotate(v, *s->rot_standard_ref, v);
	for (i = X; i <= Z; i++)
		v[i] = SENSOR_APPLY_SCALE(v[i], data->scale[i]);
}

int bmi_decode_header(struct motion_sensor_t *accel, enum fifo_header hdr,
		      uint32_t last_ts, uint8_t **bp, uint8_t *ep)
{
	if ((hdr & BMI_FH_MODE_MASK) == BMI_FH_EMPTY &&
	    (hdr & BMI_FH_PARM_MASK) != 0) {
		int i, size = 0;
		/* Check if there is enough space for the data frame */
		for (i = MOTIONSENSE_TYPE_MAG; i >= MOTIONSENSE_TYPE_ACCEL;
		     i--) {
			if (hdr & (1 << (i + BMI_FH_PARM_OFFSET)))
				size += (i == MOTIONSENSE_TYPE_MAG ? 8 : 6);
		}
		if (*bp + size > ep) {
			/* frame is not complete, it will be retransmitted. */
			*bp = ep;
			return 1;
		}
		for (i = MOTIONSENSE_TYPE_MAG; i >= MOTIONSENSE_TYPE_ACCEL;
		     i--) {
			struct motion_sensor_t *s = accel + i;

			if (hdr & (1 << (i + BMI_FH_PARM_OFFSET))) {
				int *v = s->raw_xyz;

				bmi_normalize(s, v, *bp);
				if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
				    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
					v = s->spoof_xyz;
				if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
					struct ec_response_motion_sensor_data
						vector;

					vector.flags = 0;
					vector.data[X] = v[X];
					vector.data[Y] = v[Y];
					vector.data[Z] = v[Z];
					vector.sensor_num = s - motion_sensors;
					motion_sense_fifo_stage_data(
						&vector, s, 3, last_ts);
				} else {
					motion_sense_push_raw_xyz(s);
				}
				*bp += (i == MOTIONSENSE_TYPE_MAG ? 8 : 6);
			}
		}

		return 1;
	} else {
		return 0;
	}
}

enum fifo_state {
	FIFO_HEADER,
	FIFO_DATA_SKIP,
	FIFO_DATA_TIME,
	FIFO_DATA_CONFIG,
};

#define BMI_FIFO_BUFFER 64
static uint8_t bmi_buffer[BMI_FIFO_BUFFER];

int bmi_load_fifo(struct motion_sensor_t *s, uint32_t last_ts)
{
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);
	uint16_t length;
	enum fifo_state state = FIFO_HEADER;
	uint8_t *bp = bmi_buffer;
	uint8_t *ep;
	uint32_t beginning;

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	if (!(data->flags & (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET))) {
		/*
		 * The FIFO was disabled while we were processing it.
		 *
		 * Flush potential left over:
		 * When sensor is resumed, we won't read old data.
		 */
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI_CMD_REG(V(s)),
			   BMI_CMD_FIFO_FLUSH);
		return EC_SUCCESS;
	}

	bmi_read_n(s->port, s->i2c_spi_addr_flags, BMI_FIFO_LENGTH_0(V(s)),
		   (uint8_t *)&length, sizeof(length));
	length &= BMI_FIFO_LENGTH_MASK(V(s));

	/*
	 * We have not requested timestamp, no extra frame to read.
	 * if we have too much to read, read the whole buffer.
	 */
	if (length == 0) {
		/*
		 * Disable this message on BMI260, due to this seems to always
		 * happen after we complete to read the data.
		 * TODO(chingkang): check why this happen on BMI260.
		 */
		if (V(s) == 0)
			CPRINTS("unexpected empty FIFO");
		return EC_SUCCESS;
	}

	/* Add one byte to get an empty FIFO frame.*/
	length++;

	if (length > sizeof(bmi_buffer))
		CPRINTS("unexpected large FIFO: %d", length);
	length = MIN(length, sizeof(bmi_buffer));

	bmi_read_n(s->port, s->i2c_spi_addr_flags, BMI_FIFO_DATA(V(s)),
		   bmi_buffer, length);
	beginning = *(uint32_t *)bmi_buffer;
	ep = bmi_buffer + length;
	/*
	 * FIFO is invalid when reading while the sensors are all
	 * suspended.
	 * Instead of returning the empty frame, it can return a
	 * pattern that looks like a valid header: 84 or 40.
	 * If we see those, assume the sensors have been disabled
	 * while this thread was running.
	 */
	if (beginning == 0x84848484 || (beginning & 0xdcdcdcdc) == 0x40404040) {
		CPRINTS("Suspended FIFO: accel ODR/rate: %d/%d: 0x%08x",
			BASE_ODR(s->config[SENSOR_CONFIG_AP].odr),
			BMI_GET_SAVED_DATA(s)->odr, beginning);
		return EC_SUCCESS;
	}

	while (bp < ep) {
		switch (state) {
		case FIFO_HEADER: {
			enum fifo_header hdr = *bp++;

			if (bmi_decode_header(s, hdr, last_ts, &bp, ep))
				continue;
			/* Other cases */
			hdr &= 0xdc;
			switch (hdr) {
			case BMI_FH_EMPTY:
				return EC_SUCCESS;
			case BMI_FH_SKIP:
				state = FIFO_DATA_SKIP;
				break;
			case BMI_FH_TIME:
				state = FIFO_DATA_TIME;
				break;
			case BMI_FH_CONFIG:
				state = FIFO_DATA_CONFIG;
				break;
			default:
				CPRINTS("Unknown header: 0x%02x @ %zd", hdr,
					bp - bmi_buffer);
				bmi_write8(s->port, s->i2c_spi_addr_flags,
					   BMI_CMD_REG(V(s)),
					   BMI_CMD_FIFO_FLUSH);
				return EC_ERROR_NOT_HANDLED;
			}
			break;
		}
		case FIFO_DATA_SKIP:
			CPRINTS("@ %zd - %d, skipped %d frames",
				bp - bmi_buffer, length, *bp);
			bp++;
			state = FIFO_HEADER;
			break;
		case FIFO_DATA_CONFIG:
			CPRINTS("@ %zd - %d, config change: 0x%02x",
				bp - bmi_buffer, length, *bp);
			bp++;
			if (V(s))
				state = FIFO_DATA_TIME;
			else
				state = FIFO_HEADER;
			break;
		case FIFO_DATA_TIME:
			if (bp + 3 > ep) {
				bp = ep;
				continue;
			}
			/* We are not requesting timestamp */
			CPRINTS("timestamp %d",
				(bp[2] << 16) | (bp[1] << 8) | bp[0]);
			state = FIFO_HEADER;
			bp += 3;
			break;
		default:
			CPRINTS("Unknown data: 0x%02x", *bp++);
			state = FIFO_HEADER;
		}
	}

	return EC_SUCCESS;
}

int bmi_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int ret, range_tbl_size;
	uint8_t reg_val, ctrl_reg;
	const struct bmi_accel_param_pair *ranges;

	if (s->type == MOTIONSENSE_TYPE_MAG) {
		s->current_range = range;
		return EC_SUCCESS;
	}

	ctrl_reg = BMI_RANGE_REG(s->type);
	ranges = bmi_get_range_table(s, &range_tbl_size);
	reg_val = bmi_get_reg_val(range, rnd, ranges, range_tbl_size);

	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, ctrl_reg, reg_val);
	/* Now that we have set the range, update the driver's value. */
	if (ret == EC_SUCCESS)
		s->current_range = bmi_get_engineering_val(reg_val, ranges,
							   range_tbl_size);
	return ret;
}

int bmi_get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = BMI_GET_SAVED_DATA(s);

	return data->odr;
}

int bmi_get_offset(const struct motion_sensor_t *s, int16_t *offset,
		   int16_t *temp)
{
	int i, ret = EC_SUCCESS;
	intv3_t v;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/*
		 * The offset of the accelerometer off_acc_[xyz] is a 8 bit
		 * two-complement number in units of 3.9 mg independent of the
		 * range selected for the accelerometer.
		 */
		ret = bmi_accel_get_offset(s, v);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/*
		 * The offset of the gyroscope off_gyr_[xyz] is a 10 bit
		 * two-complement number in units of 0.061 °/s.
		 * Therefore a maximum range that can be compensated is
		 * -31.25 °/s to +31.25 °/s
		 */
		ret = bmi_gyro_get_offset(s, v);
		break;
#ifdef CONFIG_MAG_BMI_BMM150
	case MOTIONSENSE_TYPE_MAG:
		ret = bmm150_get_offset(s, v);
		break;
#endif /* defined(CONFIG_MAG_BMI_BMM150) */
	default:
		for (i = X; i <= Z; i++)
			v[i] = 0;
	}

	if (ret != EC_SUCCESS)
		return ret;

	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];
	/* Saving temperature at calibration not supported yet */
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

#ifdef CONFIG_BODY_DETECTION
int bmi_get_rms_noise(const struct motion_sensor_t *accel,
		      int rms_noise_100hz_mg)
{
	fp_t rate, sqrt_rate_ratio;

	/* change unit of ODR to Hz to prevent INT_TO_FP() overflow */
	rate = INT_TO_FP(bmi_get_data_rate(accel) / 1000);
	/*
	 * Since the noise is proportional to sqrt(ODR) in BMI, and we
	 * have rms noise in 100 Hz, we multiply it with the sqrt(ratio
	 * of ODR to 100Hz) to get current noise.
	 */
	sqrt_rate_ratio = fp_sqrtf(fp_div(rate, INT_TO_FP(BMI_ACCEL_100HZ)));
	return FP_TO_INT(
		fp_mul(INT_TO_FP(rms_noise_100hz_mg), sqrt_rate_ratio));
}
#endif

int bmi_get_resolution(const struct motion_sensor_t *s)
{
	return BMI_RESOLUTION;
}

int bmi_set_scale(const struct motion_sensor_t *s, const uint16_t *scale,
		  int16_t temp)
{
	struct accelgyro_saved_data_t *data = BMI_GET_SAVED_DATA(s);

	data->scale[X] = scale[X];
	data->scale[Y] = scale[Y];
	data->scale[Z] = scale[Z];
	return EC_SUCCESS;
}

int bmi_get_scale(const struct motion_sensor_t *s, uint16_t *scale,
		  int16_t *temp)
{
	struct accelgyro_saved_data_t *data = BMI_GET_SAVED_DATA(s);

	scale[X] = data->scale[X];
	scale[Y] = data->scale[Y];
	scale[Z] = data->scale[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

int bmi_enable_fifo(const struct motion_sensor_t *s, int enable)
{
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);
	int ret;

	/* FIFO start/stop collecting events */
	ret = bmi_enable_reg8(s, BMI_FIFO_CONFIG_1(V(s)),
			      BMI_FIFO_SENSOR_EN(V(s), s->type), enable);
	if (ret)
		return ret;

	if (enable)
		data->flags |= 1 << (s->type + BMI_FIFO_FLAG_OFFSET);
	else
		data->flags &= ~(1 << (s->type + BMI_FIFO_FLAG_OFFSET));

	return ret;
}

int bmi_read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t data[6];
	int ret, status = 0;

	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI_STATUS(V(s)),
			&status);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that motion senor task can read again
	 * to get the latest updated sensor data quickly.
	 */
	if (!(status & BMI_DRDY_MASK(s->type))) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
		return EC_SUCCESS;
	}

	/* Read 6 bytes starting at xyz_reg */
	ret = bmi_read_n(s->port, s->i2c_spi_addr_flags, bmi_get_xyz_reg(s),
			 data, 6);

	if (ret != EC_SUCCESS) {
		CPRINTS("%s: type:0x%X RD XYZ Error %d", s->name, s->type, ret);
		return ret;
	}
	bmi_normalize(s, v, data);
	return EC_SUCCESS;
}

int bmi_read_temp(const struct motion_sensor_t *s, int *temp_ptr)
{
	return bmi_get_sensor_temp(s - motion_sensors, temp_ptr);
}

int bmi_get_sensor_temp(int idx, int *temp_ptr)
{
	struct motion_sensor_t *s = &motion_sensors[idx];
	int16_t temp;
	int ret;

	ret = bmi_read_n(s->port, s->i2c_spi_addr_flags,
			 BMI_TEMPERATURE_0(V(s)), (uint8_t *)&temp,
			 sizeof(temp));

	if (ret || temp == (int16_t)BMI_INVALID_TEMP)
		return EC_ERROR_NOT_POWERED;

	*temp_ptr = C_TO_K(23 + ((temp + 256) >> 9));
	return 0;
}

int bmi_get_normalized_rate(const struct motion_sensor_t *s, int rate, int rnd,
			    int *normalized_rate_ptr, uint8_t *reg_val_ptr)
{
	*reg_val_ptr = BMI_ODR_TO_REG(rate);
	*normalized_rate_ptr = BMI_REG_TO_ODR(*reg_val_ptr);
	if (rnd && (*normalized_rate_ptr < rate)) {
		(*reg_val_ptr)++;
		*normalized_rate_ptr = BMI_REG_TO_ODR(*reg_val_ptr);
	}

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		if (*normalized_rate_ptr > BMI_ACCEL_MAX_FREQ ||
		    *normalized_rate_ptr < BMI_ACCEL_MIN_FREQ)
			return EC_RES_INVALID_PARAM;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		if (*normalized_rate_ptr > BMI_GYRO_MAX_FREQ ||
		    *normalized_rate_ptr < BMI_GYRO_MIN_FREQ)
			return EC_RES_INVALID_PARAM;
		break;
#ifdef CONFIG_MAG_BMI_BMM150
	case MOTIONSENSE_TYPE_MAG:
		/* We use the regular preset we can go about 100Hz */
		if (*reg_val_ptr > BMI_ODR_100HZ ||
		    *reg_val_ptr < BMI_ODR_0_78HZ)
			return EC_RES_INVALID_PARAM;
		break;
#endif

	default:
		return EC_RES_INVALID_PARAM;
	}
	return EC_SUCCESS;
}

int bmi_accel_get_offset(const struct motion_sensor_t *accel, intv3_t v)
{
	int i, val, ret;

	for (i = X; i <= Z; i++) {
		ret = bmi_read8(accel->port, accel->i2c_spi_addr_flags,
				BMI_OFFSET_ACC70(V(accel)) + i, &val);
		if (ret != EC_SUCCESS)
			return ret;

		if (val > 0x7f)
			val = -256 + val;
		v[i] = round_divide((int64_t)val * BMI_OFFSET_ACC_MULTI_MG,
				    BMI_OFFSET_ACC_DIV_MG);
	}

	return EC_SUCCESS;
}

int bmi_gyro_get_offset(const struct motion_sensor_t *gyro, intv3_t v)
{
	int i, val, val98, ret;

	/* Read the MSB first */
	ret = bmi_read8(gyro->port, gyro->i2c_spi_addr_flags,
			BMI_OFFSET_EN_GYR98(V(gyro)), &val98);
	if (ret != EC_SUCCESS)
		return ret;

	for (i = X; i <= Z; i++) {
		ret = bmi_read8(gyro->port, gyro->i2c_spi_addr_flags,
				BMI_OFFSET_GYR70(V(gyro)) + i, &val);
		if (ret != EC_SUCCESS)
			return ret;

		val |= ((val98 >> (2 * i)) & 0x3) << 8;
		if (val > 0x1ff)
			val = -1024 + val;
		v[i] = round_divide((int64_t)val * BMI_OFFSET_GYRO_MULTI_MDS,
				    BMI_OFFSET_GYRO_DIV_MDS);
	}

	return EC_SUCCESS;
}

int bmi_set_accel_offset(const struct motion_sensor_t *accel, intv3_t v)
{
	int i, val, ret;

	for (i = X; i <= Z; ++i) {
		val = round_divide((int64_t)v[i] * BMI_OFFSET_ACC_DIV_MG,
				   BMI_OFFSET_ACC_MULTI_MG);
		if (val > 127)
			val = 127;
		if (val < -128)
			val = -128;
		if (val < 0)
			val = 256 + val;
		ret = bmi_write8(accel->port, accel->i2c_spi_addr_flags,
				 BMI_OFFSET_ACC70(V(accel)) + i, val);
		if (ret != EC_SUCCESS)
			return ret;
	}

	return EC_SUCCESS;
}

int bmi_set_gyro_offset(const struct motion_sensor_t *gyro, intv3_t v,
			int *val98_ptr)
{
	int i, val, ret;

	for (i = X; i <= Z; i++) {
		val = round_divide((int64_t)v[i] * BMI_OFFSET_GYRO_DIV_MDS,
				   BMI_OFFSET_GYRO_MULTI_MDS);
		if (val > 511)
			val = 511;
		if (val < -512)
			val = -512;
		if (val < 0)
			val = 1024 + val;
		ret = bmi_write8(gyro->port, gyro->i2c_spi_addr_flags,
				 BMI_OFFSET_GYR70(V(gyro)) + i, val & 0xFF);
		if (ret != EC_SUCCESS)
			return ret;

		*val98_ptr &= ~(0x3 << (2 * i));
		*val98_ptr |= (val >> 8) << (2 * i);
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_BMI_ORIENTATION_SENSOR
bool motion_orientation_changed(const struct motion_sensor_t *s)
{
	return BMI_GET_DATA(s)->orientation !=
	       BMI_GET_DATA(s)->last_orientation;
}

enum motionsensor_orientation *
motion_orientation_ptr(const struct motion_sensor_t *s)
{
	return &BMI_GET_DATA(s)->orientation;
}

void motion_orientation_update(const struct motion_sensor_t *s)
{
	BMI_GET_DATA(s)->last_orientation = BMI_GET_DATA(s)->orientation;
}
#endif

int bmi_list_activities(const struct motion_sensor_t *s, uint32_t *enabled,
			uint32_t *disabled)
{
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);
	*enabled = data->enabled_activities;
	*disabled = data->disabled_activities;
	return EC_RES_SUCCESS;
}
