/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI260 accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "accelgyro_bmi260.h"
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

#ifdef CONFIG_ACCELGYRO_BMI260_INT_EVENT
#define ACCELGYRO_BMI260_INT_ENABLE
#endif

/* BMI220/BMI260 firmware binary */
#if defined(CONFIG_ACCELGYRO_BMI220)
#include "bmi220/accelgyro_bmi220_config_tbin.h"
#endif /* CONFIG_ACCELGYRO_BMI220 */

#if defined(CONFIG_ACCELGYRO_BMI260)
#include "bmi260/accelgyro_bmi260_config_tbin.h"
#endif /* CONFIG_ACCELGYRO_BMI260 */

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCELGYRO_BMI260_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

/*
 * The gyro start-up time is 45ms in normal mode
 *                            2ms in fast start-up mode
 */
static int wakeup_time[] = { [MOTIONSENSE_TYPE_ACCEL] = 2,
			     [MOTIONSENSE_TYPE_GYRO] = 45,
			     [MOTIONSENSE_TYPE_MAG] = 1 };

static int enable_sensor(const struct motion_sensor_t *s, int enable)
{
	int ret;

	ret = bmi_enable_reg8(s, BMI260_PWR_CTRL, BMI260_PWR_EN(s->type),
			      enable);
	if (ret)
		return ret;

	if (s->type == MOTIONSENSE_TYPE_GYRO) {
		/* switch to performance mode */
		ret = bmi_enable_reg8(
			s, BMI_CONF_REG(s->type),
			BMI260_FILTER_PERF | BMI260_GYR_NOISE_PERF, enable);
	} else {
		ret = bmi_enable_reg8(s, BMI_CONF_REG(s->type),
				      BMI260_FILTER_PERF, enable);
	}
	return ret;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	uint8_t reg_val;
	struct accelgyro_saved_data_t *data = BMI_GET_SAVED_DATA(s);

	if (rate == 0) {
		/* FIFO stop collecting events */
		if (IS_ENABLED(ACCELGYRO_BMI260_INT_ENABLE))
			bmi_enable_fifo(s, 0);
		/* disable sensor */
		ret = enable_sensor(s, 0);
		crec_msleep(3);
		data->odr = 0;
		return ret;
	} else if (data->odr == 0) {
		/* enable sensor */
		ret = enable_sensor(s, 1);
		if (ret)
			return ret;
		/* Wait for accel/gyro to wake up */
		crec_msleep(wakeup_time[s->type]);
	}

	ret = bmi_get_normalized_rate(s, rate, rnd, &normalized_rate, &reg_val);
	if (ret)
		return ret;

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = bmi_set_reg8(s, BMI_CONF_REG(s->type), reg_val, BMI_ODR_MASK);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	/* Now that we have set the odr, update the driver's value. */
	data->odr = normalized_rate;

	/*
	 * FIFO start collecting events.
	 * They will be discarded if AP does not want them.
	 */
	if (IS_ENABLED(ACCELGYRO_BMI260_INT_ENABLE))
		bmi_enable_fifo(s, 1);
accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	int ret, val98, val_nv_conf;
	intv3_t v = { offset[X], offset[Y], offset[Z] };

	rotate_inv(v, *s->rot_standard_ref, v);

	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI260_OFFSET_EN_GYR98,
			&val98);
	if (ret)
		return ret;
	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI260_NV_CONF,
			&val_nv_conf);
	if (ret)
		return ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		ret = bmi_set_accel_offset(s, v);
		if (ret != EC_SUCCESS)
			return ret;

		ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_NV_CONF,
				 val_nv_conf | BMI260_ACC_OFFSET_EN);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		ret = bmi_set_gyro_offset(s, v, &val98);
		if (ret != EC_SUCCESS)
			return ret;

		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI260_OFFSET_EN_GYR98,
				 val98 | BMI260_OFFSET_GYRO_EN);
		break;
	default:
		ret = EC_RES_INVALID_PARAM;
	}
	return ret;
}

#ifdef CONFIG_BODY_DETECTION
static int get_rms_noise(const struct motion_sensor_t *s)
{
	return bmi_get_rms_noise(s, BMI260_ACCEL_RMS_NOISE_100HZ);
}
#endif

static int wait_and_read_data(const struct motion_sensor_t *s, intv3_t v,
			      int try_cnt, int msec)
{
	uint8_t data[6];
	int ret, status = 0;

	/* Check if data is ready */
	while (try_cnt && !(status & BMI260_DRDY_ACC)) {
		crec_msleep(msec);
		ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI260_STATUS,
				&status);
		if (ret)
			return ret;
		try_cnt -= 1;
	}
	if (!(status & BMI260_DRDY_ACC))
		return EC_ERROR_TIMEOUT;
	/* Read 6 bytes starting at xyz_reg */
	ret = bmi_read_n(s->port, s->i2c_spi_addr_flags, bmi_get_xyz_reg(s),
			 data, 6);
	bmi_normalize(s, v, data);
	return ret;
}

static int calibrate_offset(const struct motion_sensor_t *s, int range,
			    intv3_t target, int16_t *offset)
{
	int ret = EC_ERROR_UNKNOWN;
	int i, n_sample = 32;
	int data_diff[3] = { 0 };

	/* Manually offset compensation */
	for (i = 0; i < n_sample; ++i) {
		intv3_t v;
		/* Wait data for at most 3 * 10 msec */
		ret = wait_and_read_data(s, v, 3, 10);
		if (ret)
			return ret;
		data_diff[X] += v[X] - target[X];
		data_diff[Y] += v[Y] - target[Y];
		data_diff[Z] += v[Z] - target[Z];
	}

	/* The data LSB: 1000 * range / 32768 (mdps | mg)*/
	for (i = X; i <= Z; ++i)
		offset[i] -=
			((int64_t)(data_diff[i] / n_sample) * 1000 * range) >>
			15;
	return ret;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	int ret, rate;
	int16_t temp;
	int16_t offset[3];
	intv3_t target = { 0, 0, 0 };
	/* Get sensor range for calibration*/
	int range = s->current_range;

	if (!enable)
		return EC_SUCCESS;

	/* We only support accelerometers and gyroscopes */
	if (s->type != MOTIONSENSE_TYPE_ACCEL &&
	    s->type != MOTIONSENSE_TYPE_GYRO)
		return EC_RES_INVALID_PARAM;

	rate = bmi_get_data_rate(s);
	ret = set_data_rate(s, 100000, 0);
	if (ret)
		return ret;

	ret = bmi_get_offset(s, offset, &temp);
	if (ret)
		goto end_perform_calib;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		target[Z] = BMI260_ACC_DATA_PLUS_1G(range);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		break;
	/* LCOV_EXCL_START */
	default:
		/* Unreachable due to sensor type check above. */
		ASSERT(false);
		break;
		/* LCOV_EXCL_STOP */
	}

	/* Get the calibrated offset */
	ret = calibrate_offset(s, range, target, offset);
	if (ret)
		goto end_perform_calib;

	ret = set_offset(s, offset, temp);
	if (ret)
		goto end_perform_calib;

end_perform_calib:
	if (ret == EC_ERROR_TIMEOUT)
		CPRINTS("%s timeout", __func__);
	set_data_rate(s, rate, 0);
	return ret;
}

/**
 * config_interrupt - sets up the interrupt request output pin on the BMI260
 *
 * Note: this function only supports motion_sensor_t structs of type
 * MOTIONSENSE_TYPE_ACCEL and expects the caller to verify this.
 */
static __maybe_unused int config_interrupt(const struct motion_sensor_t *s)
{
	int ret;

	mutex_lock(s->mutex);
	bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_CMD_REG,
		   BMI260_CMD_FIFO_FLUSH);

	/* configure int1 as an interrupt */
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_INT1_IO_CTRL,
			 BMI260_INT1_OUTPUT_EN);
	if (IS_ENABLED(CONFIG_ACCELGYRO_BMI260_INT2_OUTPUT))
		/* TODO(chingkang): Test it if we want int2 as an interrupt */
		/* configure int2 as an interrupt */
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI260_INT2_IO_CTRL, BMI260_INT2_OUTPUT_EN);
	else
		/* configure int2 as an external input. */
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI260_INT2_IO_CTRL, BMI260_INT2_INPUT_EN);

	/* map fifo water mark to int 1 */
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_INT_MAP_DATA,
			 BMI260_INT_MAP_DATA_REG(1, FWM) |
				 BMI260_INT_MAP_DATA_REG(1, FFULL));

	/*
	 * Configure fifo watermark to int whenever there's any data in
	 * there
	 */
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_FIFO_WTM_0, 1);
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_FIFO_WTM_1, 0);
	if (IS_ENABLED(CONFIG_ACCELGYRO_BMI260_INT2_OUTPUT))
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI260_FIFO_CONFIG_1, BMI260_FIFO_HEADER_EN);
	else
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI260_FIFO_CONFIG_1,
				 (BMI260_FIFO_TAG_INT_LEVEL
				  << BMI260_FIFO_TAG_INT2_EN_OFFSET) |
					 BMI260_FIFO_HEADER_EN);
	/* disable FIFO sensortime frame */
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_FIFO_CONFIG_0,
			 0);
	mutex_unlock(s->mutex);
	return ret;
}

#ifdef ACCELGYRO_BMI260_INT_ENABLE
/**
 * bmi260_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->irq_handler().
 */
void bmi260_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCELGYRO_BMI260_INT_EVENT);
}

/**
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	/* use uint16_t interrupt can cause error. */
	uint32_t interrupt = 0;
	int8_t has_read_fifo = 0;
	int rv;
	int i;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCELGYRO_BMI260_INT_EVENT)))
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
		rv = bmi_read16(s->port, s->i2c_spi_addr_flags,
				BMI260_INT_STATUS_0, &interrupt);

		/* Bail out if there was an error or no more interrupts. */
		if (rv || !interrupt)
			break;

		if (interrupt & (BMI260_FWM_INT | BMI260_FFULL_INT)) {
			bmi_load_fifo(s, last_interrupt_timestamp);
			has_read_fifo = 1;
		}
	}

	if (i == 200) {
		CPRINTF("BMI260 irq 0x%04x stuck (%d loops)\n", interrupt, i);
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_CMD_REG,
			   BMI260_CMD_FIFO_FLUSH);
	}

	/* Only return an error if no data was read at all. */
	if (i == 0 && rv)
		return rv;

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && has_read_fifo)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif /* ACCELGYRO_BMI260_INT_ENABLE */

/*
 * If the .init_rom section is not memory mapped, we need a static
 * buffer in RAM to access the BMI configuration data.
 */
#ifdef CONFIG_CHIP_INIT_ROM_REGION
#define BMI_RAM_BUFFER_SIZE 256
static uint8_t bmi_ram_buffer[BMI_RAM_BUFFER_SIZE];
#else
#define BMI_RAM_BUFFER_SIZE 0
static uint8_t *bmi_ram_buffer;
#endif

static int bmi_config_load(const struct motion_sensor_t *s)
{
	int ret = EC_SUCCESS;
	uint16_t i;
	const uint8_t *bmi_config = NULL;
	const unsigned char *bmi_config_tbin;
	int bmi_config_tbin_len;
	/*
	 * Due to i2c transaction timeout limit,
	 * burst_write_len should not be above 2048 to prevent timeout.
	 */
	int burst_write_len = 2048;

	/*
	 * The BMI config data may be linked into .rodata or the .init_rom
	 * section. Get the actual memory mapped address.
	 */
	switch (s->chip) {
#ifdef CONFIG_ACCELGYRO_BMI220
	case MOTIONSENSE_CHIP_BMI220:
		bmi_config_tbin = g_bmi220_config_tbin;
		bmi_config_tbin_len = g_bmi220_config_tbin_len;
		break;
#endif /* CONFIG_ACCELGYRO_BMI220 */

#ifdef CONFIG_ACCELGYRO_BMI260
	case MOTIONSENSE_CHIP_BMI260:
		bmi_config_tbin = g_bmi260_config_tbin;
		bmi_config_tbin_len = g_bmi260_config_tbin_len;
		break;
#endif /* CONFIG_ACCELGYRO_BMI260 */

	default:
		return EC_ERROR_INVALID_CONFIG;
	}

	bmi_config = init_rom_map(bmi_config_tbin, bmi_config_tbin_len);

	/*
	 * init_rom_map() only returns NULL when the CONFIG_CHIP_INIT_ROM_REGION
	 * option is enabled and flash memory is not memory mapped.  In this
	 * case copy the BMI config data through a RAM buffer and limit the
	 * I2C burst to the size of the RAM buffer.
	 */
	if (!bmi_config)
		burst_write_len = MIN(BMI_RAM_BUFFER_SIZE, burst_write_len);

	/* We have to write the config even bytes of data every time */
	ASSERT(((burst_write_len & 1) == 0) && (burst_write_len != 0));

	for (i = 0; i < bmi_config_tbin_len; i += burst_write_len) {
		uint8_t addr[2];
		const int len = MIN(burst_write_len, bmi_config_tbin_len - i);

		addr[0] = (i / 2) & 0xF;
		addr[1] = (i / 2) >> 4;
		ret = bmi_write_n(s->port, s->i2c_spi_addr_flags,
				  BMI260_INIT_ADDR_0, addr, 2);
		if (ret)
			break;

		if (!bmi_config) {
			/*
			 * init_rom region isn't memory mapped. Copy the
			 * data through a RAM buffer.
			 */
			ret = init_rom_copy((int)&bmi_config_tbin[i], len,
					    bmi_ram_buffer);
			if (ret)
				break;

			ret = bmi_write_n(s->port, s->i2c_spi_addr_flags,
					  BMI260_INIT_DATA, bmi_ram_buffer,
					  len);
		} else {
			ret = bmi_write_n(s->port, s->i2c_spi_addr_flags,
					  BMI260_INIT_DATA, &bmi_config[i],
					  len);
		}

		if (ret)
			break;
	}

	/*
	 * Unmap the BMI config data, required when init_rom_map() returns
	 * a non NULL value.
	 */
	if (bmi_config)
		init_rom_unmap(bmi_config_tbin, bmi_config_tbin_len);

	return ret;
}

static int init_config(const struct motion_sensor_t *s)
{
	int init_status, ret;
	uint16_t i;

	/* disable advance power save but remain fifo self wakeup*/
	bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_PWR_CONF, 2);
	crec_msleep(1);
	/* prepare for config load */
	bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_INIT_CTRL, 0);

	/* load config file to INIT_DATA */
	ret = bmi_config_load(s);

	/* finish config load */
	bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_INIT_CTRL, 1);
	/* return error if load config failed */
	if (ret)
		return ret;
	/* wait INTERNAL_STATUS.message to be 0x1 which take at most 150ms */
	for (i = 0; i < 15; ++i) {
		crec_msleep(10);
		ret = bmi_read8(s->port, s->i2c_spi_addr_flags,
				BMI260_INTERNAL_STATUS, &init_status);
		if (ret)
			break;
		init_status &= BMI260_MESSAGE_MASK;
		if (init_status == BMI260_INIT_OK)
			break;
	}
	if (ret || init_status != BMI260_INIT_OK)
		return EC_ERROR_INVALID_CONFIG;
	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp, i;
	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI260_CHIP_ID, &tmp);
	if (ret)
		return EC_ERROR_UNKNOWN;

	switch (s->chip) {
	case MOTIONSENSE_CHIP_BMI220:
		if (tmp != BMI220_CHIP_ID_MAJOR)
			return EC_ERROR_ACCESS_DENIED;
		break;

	case MOTIONSENSE_CHIP_BMI260:
		if (tmp != BMI260_CHIP_ID_MAJOR)
			return EC_ERROR_ACCESS_DENIED;
		break;

	default:
		return EC_ERROR_ACCESS_DENIED;
	}

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		struct bmi_drv_data_t *data = BMI_GET_DATA(s);

		/* Reset the chip to be in a good state */
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI260_CMD_REG,
			   BMI260_CMD_SOFT_RESET);
		crec_msleep(2);
		if (init_config(s))
			return EC_ERROR_INVALID_CONFIG;

		data->flags &= ~(BMI_FLAG_SEC_I2C_ENABLED |
				 (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET));
	}

	for (i = X; i <= Z; i++)
		saved_data->scale[i] = MOTION_SENSE_DEFAULT_SCALE;
	/*
	 * The sensor is in Suspend mode at init,
	 * so set data rate to 0.
	 */
	saved_data->odr = 0;

	if (IS_ENABLED(ACCELGYRO_BMI260_INT_ENABLE) &&
	    (s->type == MOTIONSENSE_TYPE_ACCEL))
		ret = config_interrupt(s);

	return sensor_init_done(s);
}

const struct accelgyro_drv bmi260_drv = {
	.init = init,
	.read = bmi_read,
	.set_range = bmi_set_range,
	.get_resolution = bmi_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = bmi_get_data_rate,
	.set_offset = set_offset,
	.get_scale = bmi_get_scale,
	.set_scale = bmi_set_scale,
	.get_offset = bmi_get_offset,
	.perform_calib = perform_calib,
	.read_temp = bmi_read_temp,
#ifdef ACCELGYRO_BMI260_INT_ENABLE
	.irq_handler = irq_handler,
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.list_activities = bmi_list_activities,
#endif
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
struct i2c_stress_test_dev bmi260_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = BMI260_CHIP_ID,
		.read_val = BMI260_CHIP_ID_MAJOR,
		.write_reg = BMI260_PMU_TRIGGER,
	},
	.i2c_read = &bmi_read8,
	.i2c_write = &bmi_write8,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_ACCEL */
