/* Copyright 2021 The ChromiumOS Authors
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
#include "builtin/assert.h"
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

#ifdef CONFIG_ACCELGYRO_BMI3XX_INT_EVENT
#define ACCELGYRO_BMI3XX_INT_ENABLE
#endif

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

#define OFFSET_UPDATE_PER_TRY 10

/* Sensor definition */
STATIC_IF(CONFIG_BMI_ORIENTATION_SENSOR)
void irq_set_orientation(struct motion_sensor_t *s);

STATIC_IF(ACCELGYRO_BMI3XX_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

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

#ifdef ACCELGYRO_BMI3XX_INT_ENABLE

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

#endif /* CONFIG_BMI_ORIENTATION_SENSOR */

/*
 * bmi3xx_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->irq_handler().
 */
test_mockable void bmi3xx_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCELGYRO_BMI3XX_INT_EVENT);
}

static int enable_fifo(const struct motion_sensor_t *s, int enable)
{
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);
	/* Set FIFO config to enable accel gyro data */
	uint8_t reg_data[4];

	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_FIFO_CONF, reg_data, 4));

	if (enable) {
		if (s->type == MOTIONSENSE_TYPE_ACCEL)
			reg_data[3] |= BMI3_FIFO_ACC_EN;
		else
			reg_data[3] |= BMI3_FIFO_GYR_EN;

		data->flags |= 1 << (s->type + BMI_FIFO_FLAG_OFFSET);
	} else {
		if (s->type == MOTIONSENSE_TYPE_ACCEL)
			reg_data[3] &= ~BMI3_FIFO_ACC_EN;
		else
			reg_data[3] &= ~BMI3_FIFO_GYR_EN;

		data->flags &= ~(1 << (s->type + BMI_FIFO_FLAG_OFFSET));
	}

	return bmi3_write_n(s, BMI3_REG_FIFO_CONF, &reg_data[2], 2);
}

static int config_interrupt(const struct motion_sensor_t *s)
{
	int ret;
	uint8_t reg_data[6] = { 0 };

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
		reg_data[2] =
			BMI3_SET_BITS(reg_data[2], BMI3_ORIENT_INT, BMI3_INT1);
	}

	ret = bmi3_write_n(s, BMI3_REG_INT_MAP1, &reg_data[2], 4);
	if (ret)
		goto err_unlock;

	/* Set FIFO water-mark to read data whenever available */
	reg_data[0] = BMI3_FIFO_ENTRY;
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

	reg_data[2] =
		BMI3_SET_BITS(reg_data[2], BMI3_INT1_OD, BMI3_INT_PUSH_PULL);

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

static void bmi3_parse_fifo_data(struct motion_sensor_t *s,
				 struct bmi3_fifo_frame *fifo_frame,
				 uint32_t last_ts)
{
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);
	uint16_t reg_data;
	int i;

	/* Start index for FIFO parsing after I2C sync word removal */
	size_t fifo_index = 1;

	/* Variable to store I2C sync data which will get in FIFO data */
	uint16_t i2c_sync_data, fifo_size;

	if (!(data->flags & (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET))) {
		/*
		 * The FIFO was disabled while we were processing it
		 * Flush potential left over:
		 * When sensor is resumed, we won't read old data.
		 */

		/* Clear the FIFO using Flush command */
		reg_data = BMI3_ENABLE;
		bmi3_write_n(s, BMI3_REG_FIFO_CTRL, (uint8_t *)&reg_data, 2);
		return;
	}

	/* Parse the length of data read excluding I2C sync word */
	fifo_size = fifo_frame->available_fifo_len - 1;

	while (fifo_size > 0) {
		for (i = 0; i < NUM_OF_PRIMARY_SENSOR; i++) {
			struct motion_sensor_t *sens_output = s + i;
			int *v = sens_output->raw_xyz;

			if (data->flags & BIT(i + BMI_FIFO_FLAG_OFFSET)) {
				/*
				 * In-case of FIFO read fail it has only
				 * 0x8000.
				 */
				if (fifo_frame->data[fifo_index] == 0x8000)
					return;

				/*
				 * In case the frame has been cut, FIFO was
				 * greater than our buffer.
				 * When a frame is only partially read out, it
				 * is retransmitted at the next readout.
				 * No need to process it here.
				 */
				if (fifo_size < BMI3_FIFO_ENTRY)
					return;

				/* Frame is complete, but may have no data. */
				fifo_size -= BMI3_FIFO_ENTRY;
				i2c_sync_data = fifo_frame->data[fifo_index++];
				if (i2c_sync_data ==
				    BMI3_FIFO_ACCEL_I2C_SYNC_FRAME + i) {
					fifo_index += 2;
					continue;
				}

				v[X] = i2c_sync_data;
				v[Y] = fifo_frame->data[fifo_index++];
				v[Z] = fifo_frame->data[fifo_index++];

				rotate(v, *sens_output->rot_standard_ref, v);

				if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
				    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
					v = sens_output->spoof_xyz;
				if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
					struct ec_response_motion_sensor_data
						vect;

					vect.data[X] = v[X];
					vect.data[Y] = v[Y];
					vect.data[Z] = v[Z];
					vect.flags = 0;
					vect.sensor_num =
						sens_output - motion_sensors;
					motion_sense_fifo_stage_data(
						&vect, sens_output, 3, last_ts);
				} else {
					motion_sense_push_raw_xyz(sens_output);
				}
			}
		}
	}
}

/*
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	bool has_read_fifo = false;
	uint16_t int_status[2];
	uint16_t reg_data[2];
	struct bmi3_fifo_frame fifo_frame;
	int rv;
	int i;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCELGYRO_BMI3XX_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	/*
	 * We have to loop until we see the interrupt status as 0 to avoid
	 * getting stuck. We use edge triggered interrupts and, once one
	 * triggers, our irq apparently won't necessarily trigger again until
	 * we've cleared all interrupt sources and then a new interrupt happens.
	 *
	 * However, despite needing to loop, we also don't want to get stuck
	 * in an infinite loop if there's a bug in the driver or the hardware.
	 * We'll loop 200 times and then give up if an interrupt is still
	 * pending.
	 */
	for (i = 0; i < 200; i++) {
		rv = bmi3_read_n(s, BMI3_REG_INT_STATUS_INT1,
				 (uint8_t *)int_status, 4);
		if (rv)
			break;

		if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR) &&
		    (BMI3_INT_STATUS_ORIENTATION & int_status[1]))
			irq_set_orientation(s);

		if ((int_status[1] &
		     (BMI3_INT_STATUS_FWM | BMI3_INT_STATUS_FFULL)) == 0)
			break;

		/* Get the FIFO fill level in words */
		rv = bmi3_read_n(s, BMI3_REG_FIFO_FILL_LVL, (uint8_t *)reg_data,
				 4);
		if (rv)
			break;

		reg_data[1] =
			BMI3_GET_BIT_POS0(reg_data[1], BMI3_FIFO_FILL_LVL);

		/* Add space for the initial 16bit read. */
		fifo_frame.available_fifo_len = reg_data[1] + 1;

		/*
		 * If fill level is greater than buffer size then wrap it to
		 * buffer size.
		 */
		if (fifo_frame.available_fifo_len > ARRAY_SIZE(fifo_frame.data))
			CPRINTS("unexpected large FIFO: %d",
				fifo_frame.available_fifo_len);

		fifo_frame.available_fifo_len =
			MIN(fifo_frame.available_fifo_len,
			    ARRAY_SIZE(fifo_frame.data));
		/* Read FIFO data */
		rv = bmi3_read_n(
			s, BMI3_REG_FIFO_DATA, (uint8_t *)fifo_frame.data,
			fifo_frame.available_fifo_len * sizeof(uint16_t));
		if (rv)
			break;

		bmi3_parse_fifo_data(s, &fifo_frame, last_interrupt_timestamp);
		has_read_fifo = true;
	}

	if (i == 200) {
		CPRINTF("irq 0x%04x stuck (%d loops)\n", int_status[1], i);

		/* Clear the FIFO using Flush command */
		reg_data[0] = BMI3_ENABLE;
		bmi3_write_n(s, BMI3_REG_FIFO_CTRL, (uint8_t *)reg_data, 2);
	}

	/* Only return an error if no data was read at all. */
	if (i == 0 && rv)
		return rv;

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && has_read_fifo)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif /* ACCELGYRO_BMI3XX_INT_ENABLE */

static int read_temp(const struct motion_sensor_t *s, int *temp_ptr)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int get_gyro_offset(const struct motion_sensor_t *s, intv3_t v)
{
	int i;
	uint8_t reg_data[14] = { 0 };

	/* Get the accel offset values */
	RETURN_ERROR(bmi3_read_n(s, BMI3_GYR_DP_OFF_X, reg_data, 14));

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

static int write_gyro_offset(const struct motion_sensor_t *s, int *val)
{
	uint8_t reg_data[6] = { 0 };

	/* x-axis offset */
	reg_data[0] = (uint8_t)(val[0] & BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((val[0] & 0x0300) >> 8);
	/* y-axis offset */
	reg_data[2] = (uint8_t)(val[1] & BMI3_SET_LOW_BYTE);
	reg_data[3] = (uint8_t)((val[1] & 0x0300) >> 8);
	/* z-axis offset */
	reg_data[4] = (uint8_t)(val[2] & BMI3_SET_LOW_BYTE);
	reg_data[5] = (uint8_t)((val[2] & 0x0300) >> 8);

	/* Update the gyro data path offset registers */
	RETURN_ERROR(bmi3_write_n(s, BMI3_GYR_DP_OFF_X, &reg_data[0], 2));
	RETURN_ERROR(bmi3_write_n(s, BMI3_GYR_DP_OFF_Y, &reg_data[2], 2));
	RETURN_ERROR(bmi3_write_n(s, BMI3_GYR_DP_OFF_Z, &reg_data[4], 2));

	return EC_SUCCESS;
}

int set_gyro_offset(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t reg_data[4] = { 0 };
	uint8_t saved_conf[6] = { 0 };
	int i, val[3];

	for (i = X; i <= Z; ++i) {
		val[i] = round_divide((int64_t)v[i] * BMI_OFFSET_GYRO_DIV_MDS,
				      BMI3_OFFSET_GYR_MDPS);
		if (val[i] > 511)
			val[i] = 511;
		if (val[i] < -512)
			val[i] = -512;
		if (val[i] < 0)
			val[i] = 1024 + val[i];
	}

	/* Set the power mode as suspend */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_ACC_CONF, saved_conf, 6));

	/* Disable accelerometer and gyroscope */
	reg_data[0] = saved_conf[2];
	reg_data[1] = 0x00;
	reg_data[2] = saved_conf[4];
	reg_data[3] = 0x00;
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_ACC_CONF, reg_data, 4));

	/* Set the gyro offset in the sensor registers */
	RETURN_ERROR(write_gyro_offset(s, val));

	/* Restore ACC_CONF by storing saved_conf data */
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_ACC_CONF, &saved_conf[2], 4));

	return EC_SUCCESS;
}

int get_accel_offset(const struct motion_sensor_t *s, intv3_t v)
{
	int i;
	uint8_t reg_data[14] = { 0 };

	/* Get the accel offset values from user registers */
	RETURN_ERROR(bmi3_read_n(s, BMI3_ACC_DP_OFF_X, reg_data, 14));

	v[0] = ((uint16_t)(reg_data[3] << 8) | reg_data[2]) & 0x3FFF;
	v[1] = ((uint16_t)(reg_data[7] << 8) | reg_data[6]) & 0x3FFF;
	v[2] = ((uint16_t)(reg_data[11] << 8) | reg_data[10]) & 0x3FFF;

	for (i = X; i <= Z; ++i) {
		if (v[i] > 0x1FFF)
			v[i] = -16384 + v[i];

		v[i] = round_divide((int64_t)v[i] * BMI3_OFFSET_ACC_MULTI_MG,
				    BMI_OFFSET_ACC_DIV_MG);
	}

	return EC_SUCCESS;
}

static int write_accel_offsets(const struct motion_sensor_t *s, int *val)
{
	uint8_t reg_data[6] = { 0 };

	/* x-axis offset */
	reg_data[0] = (uint8_t)(val[0] & BMI3_SET_LOW_BYTE);
	reg_data[1] = (uint8_t)((val[0] & 0x3F00) >> 8);
	/* y-axis offset */
	reg_data[2] = (uint8_t)(val[1] & BMI3_SET_LOW_BYTE);
	reg_data[3] = (uint8_t)((val[1] & 0x3F00) >> 8);
	/* z-axis offset */
	reg_data[4] = (uint8_t)(val[2] & BMI3_SET_LOW_BYTE);
	reg_data[5] = (uint8_t)((val[2] & 0x3F00) >> 8);

	/* Update the acc data path offset registers */
	RETURN_ERROR(bmi3_write_n(s, BMI3_ACC_DP_OFF_X, &reg_data[0], 2));
	RETURN_ERROR(bmi3_write_n(s, BMI3_ACC_DP_OFF_Y, &reg_data[2], 2));
	RETURN_ERROR(bmi3_write_n(s, BMI3_ACC_DP_OFF_Z, &reg_data[4], 2));

	return EC_SUCCESS;
}

int set_accel_offset(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t reg_data[4] = { 0 };
	uint8_t saved_conf[6] = { 0 };
	int i, val[3];

	for (i = X; i <= Z; ++i) {
		val[i] = round_divide((int64_t)v[i] * BMI_OFFSET_ACC_DIV_MG,
				      BMI3_OFFSET_ACC_MULTI_MG);
		if (val[i] > 8191)
			val[i] = 8191;
		if (val[i] < -8192)
			val[i] = -8192;
		if (val[i] < 0)
			val[i] += 16384;
	}

	/* Set the power mode as suspend */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_ACC_CONF, saved_conf, 6));

	/* Disable accelerometer and gyroscope */
	reg_data[0] = saved_conf[2];
	reg_data[1] = 0x00;
	reg_data[2] = saved_conf[4];
	reg_data[3] = 0x00;
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_ACC_CONF, reg_data, 4));

	/* Set the accel offset in the sensor registers */
	RETURN_ERROR(write_accel_offsets(s, val));

	/* Restore ACC_CONF by storing saved_conf data */
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_ACC_CONF, &saved_conf[2], 4));

	return EC_SUCCESS;
}

static int set_gyro_foc_config(struct motion_sensor_t *s)
{
	uint8_t reg_data[4] = { 0 };
	uint8_t base_addr[2] = { BMI3_BASE_ADDR_SC, 0 };

	/*
	 * Set the FOC base address to feature engine
	 * transmission address to start DMA transaction
	 */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX, base_addr, 2));

	/* Read the configuration from the feature engine register */
	RETURN_ERROR(
		bmi3_read_n(s, BMI3_FEATURE_ENGINE_DMA_TX_DATA, reg_data, 4));
	/* Enable self calibration */
	reg_data[2] |= 0x07;

	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX, base_addr, 2));

	/* Set the configuration to the feature engine register */
	RETURN_ERROR(bmi3_write_n(s, BMI3_FEATURE_ENGINE_DMA_TX_DATA,
				  &reg_data[2], 2));

	/* Trigger bmi3 gyro self calibration */
	reg_data[0] = (uint8_t)(BMI3_CMD_SELF_CALIB & BMI3_SET_LOW_BYTE);
	reg_data[1] =
		(uint8_t)((BMI3_CMD_SELF_CALIB & BMI3_SET_HIGH_BYTE) >> 8);

	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_CMD, reg_data, 2));

	return EC_SUCCESS;
}

static int get_calib_result(struct motion_sensor_t *s)
{
	uint8_t i, reg_data[4];

	for (i = 0; i < 25; i++) {
		/* A delay of 120ms is required to read this status register */
		crec_msleep(OFFSET_UPDATE_DELAY);

		/* Read the configuration from the feature engine register */
		RETURN_ERROR(bmi3_read_n(s, BMI3_FEATURE_IO_1, reg_data, 4));

		switch (s->type) {
		case MOTIONSENSE_TYPE_GYRO:
			if (reg_data[2] & BMI3_SC_ST_STATUS_MASK) {
				/* Check calibration result */
				if (reg_data[2] & BMI3_SC_RESULT_MASK)
					return EC_SUCCESS;
			}
			break;
		default:
			return EC_ERROR_UNIMPLEMENTED;
		}
	}

	return EC_ERROR_NOT_CALIBRATED;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	int ret;
	uint8_t saved_conf[6] = { 0 };

	/* Variable to set the accelerometer configuration value 50Hz for FOC */
	uint8_t acc_conf_data[2] = { BMI3_FOC_ACC_CONF_VAL_LSB,
				     BMI3_FOC_ACC_CONF_VAL_MSB };

	if (!enable)
		return EC_SUCCESS;

	/* Get default configurations for the type of feature selected. */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_ACC_CONF, saved_conf, 6));

	ret = bmi3_write_n(s, BMI3_REG_ACC_CONF, acc_conf_data, 2);
	if (ret)
		goto end_calib;

	crec_msleep(FOC_DELAY);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		ret = EC_RES_INVALID_COMMAND;
		goto end_calib;
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
	/* Restore ACC_CONF before exiting */
	RETURN_ERROR(bmi3_write_n(s, BMI3_REG_ACC_CONF, &saved_conf[2], 4));

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

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	int ret;
	intv3_t v = { offset[X], offset[Y], offset[Z] };
	(void)temp;

	rotate_inv(v, *s->rot_standard_ref, v);

	/*
	 * Lock accel resource to prevent I2C racing condition.
	 */
	mutex_lock(s->mutex);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* Offset should be in units of mg */
		ret = set_accel_offset(s, v);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* Offset should be in units of mdps */
		ret = set_gyro_offset(s, v);
		break;
	default:
		ret = EC_RES_INVALID_PARAM;
	}

	mutex_unlock(s->mutex);
	return ret;
}

#ifdef CONFIG_BODY_DETECTION
static int get_rms_noise(const struct motion_sensor_t *s)
{
	return bmi_get_rms_noise(s, BMI3_ACCEL_RMS_NOISE_100HZ);
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

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret;
	int normalized_rate = 0;
	uint8_t reg_data[4];
	uint8_t reg_val = 0;

	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	if (rate > 0)
		RETURN_ERROR(bmi_get_normalized_rate(
			s, rate, rnd, &normalized_rate, &reg_val));

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
			if (IS_ENABLED(ACCELGYRO_BMI3XX_INT_ENABLE))
				ret = enable_fifo(s, 0);

			/*
			 * Disable accel to set rate equal to zero.
			 * Accel does not have suspend mode.
			 */
			reg_data[3] = BMI3_SET_BITS(reg_data[3],
						    BMI3_POWER_MODE,
						    BMI3_ACC_MODE_DISABLE);

			saved_data->odr = 0;
		} else if (saved_data->odr == 0) {
			/*
			 * Power mode changed from suspend to
			 * normal
			 */
			reg_data[3] = BMI3_SET_BITS(reg_data[3],
						    BMI3_POWER_MODE,
						    BMI3_ACC_MODE_NORMAL);
		}
	} else if (s->type == MOTIONSENSE_TYPE_GYRO) {
		if (rate == 0) {
			/* FIFO stop collecting events */
			if (IS_ENABLED(ACCELGYRO_BMI3XX_INT_ENABLE))
				ret = enable_fifo(s, 0);

			/*
			 * Set gyro to suspend mode to disable gyro
			 * however keep internal driver enabled
			 */
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
	}

	/* Set accelerometer ODR */
	reg_data[2] = BMI3_SET_BIT_POS0(reg_data[2], BMI3_SENS_ODR, reg_val);

	/* Set the accel/gyro configurations. */
	ret = bmi3_write_n(s, BMI3_REG_ACC_CONF + s->type, &reg_data[2], 2);
	if (ret) {
		mutex_unlock(s->mutex);
		return ret;
	}

	saved_data->odr = normalized_rate;

	/*
	 * If rate is non zero, FIFO start collecting events.
	 * They will be discarded if AP does not want them.
	 */
	if (IS_ENABLED(ACCELGYRO_BMI3XX_INT_ENABLE) && (rate > 0))
		ret = enable_fifo(s, 1);

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
	int(*sensor_range)[2];

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
			if (rnd) {
				index++;
			}
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
		ret = bmi3_write_n(s, BMI3_REG_ACC_CONF + s->type, &reg_data[2],
				   2);

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

	/*
	 * BMI3xx driver only supports MOTIONSENSE_TYPE_ACCEL and
	 * MOTIONSENSE_TYPE_GYRO
	 */
	if (s->type != MOTIONSENSE_TYPE_ACCEL &&
	    s->type != MOTIONSENSE_TYPE_GYRO)
		return EC_ERROR_UNIMPLEMENTED;

	/* Read chip id */
	RETURN_ERROR(bmi3_read_n(s, BMI3_REG_CHIP_ID, reg_data, 4));

	if (reg_data[2] != BMI323_CHIP_ID)
		return EC_ERROR_HW_INTERNAL;

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		/* Reset bmi3 device */
		reg_data[0] =
			(uint8_t)(BMI3_CMD_SOFT_RESET & BMI3_SET_LOW_BYTE);
		reg_data[1] =
			(uint8_t)((BMI3_CMD_SOFT_RESET & BMI3_SET_HIGH_BYTE) >>
				  8);

		RETURN_ERROR(bmi3_write_n(s, BMI3_REG_CMD, reg_data, 2));

		/* Delay of 2ms after soft reset*/
		crec_msleep(2);

		/* Enable feature engine bit */
		reg_data[0] = BMI3_ENABLE;
		reg_data[1] = 0;

		RETURN_ERROR(bmi3_write_n(s, BMI3_REG_FEATURE_ENGINE_GLOB_CTRL,
					  reg_data, 2));

		if (IS_ENABLED(ACCELGYRO_BMI3XX_INT_ENABLE))
			RETURN_ERROR(config_interrupt(s));
	}

	for (i = X; i <= Z; i++)
		saved_data->scale[i] = MOTION_SENSE_DEFAULT_SCALE;

	/* The sensor is in Suspend mode at init, so set data rate to 0*/
	saved_data->odr = 0;

	/* Flags used in FIFO parsing */
	data->flags &= ~(BMI_FLAG_SEC_I2C_ENABLED |
			 (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET));

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
#ifdef ACCELGYRO_BMI3XX_INT_ENABLE
	.irq_handler = irq_handler,
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.list_activities = bmi_list_activities,
#endif
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
};
