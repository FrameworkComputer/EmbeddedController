/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* KXCJ9 gsensor module for Chrome EC */

#include "accelerometer.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kxcj9.h"
#include "gpio.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/* Number of times to attempt to enable sensor before giving up. */
#define SENSOR_ENABLE_ATTEMPTS 3

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
const struct accel_param_pair ranges[] = {
	{2, KXCJ9_GSEL_2G},
	{4, KXCJ9_GSEL_4G},
	{8, KXCJ9_GSEL_8G_14BIT}
};

/* List of resolution values in bits and their associated register values. */
const struct accel_param_pair resolutions[] = {
	{8,  KXCJ9_RES_8BIT},
	{12, KXCJ9_RES_12BIT}
};

/* List of ODR values in mHz and their associated register values. */
const struct accel_param_pair datarates[] = {
	{781,     KXCJ9_OSA_0_781HZ},
	{1563,    KXCJ9_OSA_1_563HZ},
	{3125,    KXCJ9_OSA_3_125HZ},
	{6250,    KXCJ9_OSA_6_250HZ},
	{12500,   KXCJ9_OSA_12_50HZ},
	{25000,   KXCJ9_OSA_25_00HZ},
	{50000,   KXCJ9_OSA_50_00HZ},
	{100000,  KXCJ9_OSA_100_0HZ},
	{200000,  KXCJ9_OSA_200_0HZ},
	{400000,  KXCJ9_OSA_400_0HZ},
	{800000,  KXCJ9_OSA_800_0HZ},
	{1600000, KXCJ9_OSA_1600_HZ}
};

/* Current range of each accelerometer. The value is an index into ranges[]. */
static int sensor_range[ACCEL_COUNT] = {0, 0};

/*
 * Current resolution of each accelerometer. The value is an index into
 * resolutions[].
 */
static int sensor_resolution[ACCEL_COUNT] = {1, 1};

/*
 * Current output data rate of each accelerometer. The value is an index into
 * datarates[].
 */
static int sensor_datarate[ACCEL_COUNT] = {6, 6};


static struct mutex accel_mutex[ACCEL_COUNT];

/**
 * Find index into a accel_param_pair that matches the given engineering value
 * passed in. The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid index. If the request is
 * outside the range of values, it returns the closest valid index.
 */
static int find_param_index(const int eng_val, const int round_up,
		const struct accel_param_pair *pairs, const int size)
{
	int i;

	/* Linear search for index to match. */
	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			return i;

		if (eng_val < pairs[i+1].val) {
			if (round_up)
				return i + 1;
			else
				return i;
		}
	}

	return i;
}

/**
 * Read register from accelerometer.
 */
static int raw_read8(const int addr, const int reg, int *data_ptr)
{
	return i2c_read8(I2C_PORT_ACCEL, addr, reg, data_ptr);
}

/**
 * Write register from accelerometer.
 */
static int raw_write8(const int addr, const int reg, int data)
{
	return i2c_write8(I2C_PORT_ACCEL, addr, reg, data);
}

/**
 * Disable sensor by taking it out of operating mode. When disabled, the
 * acceleration data does not change.
 *
 * Note: This is intended to be called in a pair with enable_sensor().
 *
 * @id Sensor index
 * @ctrl1 Pointer to location to store KXCJ9_CTRL1 register after disabling
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int disable_sensor(const enum accel_id id, int *ctrl1)
{
	int ret;

	/*
	 * Read the current state of the ctrl1 register so that we can restore
	 * it later.
	 */
	ret = raw_read8(accel_addr[id], KXCJ9_CTRL1, ctrl1);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Before disabling the sensor, acquire mutex to prevent another task
	 * from attempting to access accel parameters until we enable sensor.
	 */
	mutex_lock(&accel_mutex[id]);

	/* Disable sensor. */
	*ctrl1 &= ~KXCJ9_CTRL1_PC1;
	ret = raw_write8(accel_addr[id], KXCJ9_CTRL1, *ctrl1);
	if (ret != EC_SUCCESS) {
		mutex_unlock(&accel_mutex[id]);
		return ret;
	}

	return EC_SUCCESS;
}

/**
 * Enable sensor by placing it in operating mode.
 *
 * Note: This is intended to be called in a pair with disable_sensor().
 *
 * @id Sensor index
 * @ctrl1 Value of KXCJ9_CTRL1 register to write to sensor
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int enable_sensor(const enum accel_id id, const int ctrl1)
{
	int i, ret;

	for (i = 0; i < SENSOR_ENABLE_ATTEMPTS; i++) {
		/* Enable accelerometer based on ctrl1 value. */
		ret = raw_write8(accel_addr[id], KXCJ9_CTRL1,
				ctrl1 | KXCJ9_CTRL1_PC1);

		/* On first success, we are done. */
		if (ret == EC_SUCCESS) {
			mutex_unlock(&accel_mutex[id]);
			return EC_SUCCESS;
		}

	}

	/* Release mutex. */
	mutex_unlock(&accel_mutex[id]);

	/* Cannot enable accel, print warning and return an error. */
	CPRINTF("[%T Error trying to enable accelerometer %d]\n", id);

	return ret;
}

int accel_set_range(const enum accel_id id, const int range, const int rnd)
{
	int ret, ctrl1, ctrl1_new, index;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Find index for interface pair matching the specified range. */
	index = find_param_index(range, rnd, ranges, ARRAY_SIZE(ranges));

	/* Disable the sensor to allow for changing of critical parameters. */
	ret = disable_sensor(id, &ctrl1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Determine new value of CTRL1 reg and attempt to write it. */
	ctrl1_new = (ctrl1 & ~KXCJ9_GSEL_ALL) | ranges[index].reg;
	ret = raw_write8(accel_addr[id],  KXCJ9_CTRL1, ctrl1_new);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS) {
		sensor_range[id] = index;
		ctrl1 = ctrl1_new;
	}

	/* Re-enable the sensor. */
	if (enable_sensor(id, ctrl1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	return ret;
}

int accel_get_range(const enum accel_id id, int * const range)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	*range = ranges[sensor_range[id]].val;
	return EC_SUCCESS;
}

int accel_set_resolution(const enum accel_id id, const int res, const int rnd)
{
	int ret, ctrl1, ctrl1_new, index;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Find index for interface pair matching the specified resolution. */
	index = find_param_index(res, rnd, resolutions,
			ARRAY_SIZE(resolutions));

	/* Disable the sensor to allow for changing of critical parameters. */
	ret = disable_sensor(id, &ctrl1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Determine new value of CTRL1 reg and attempt to write it. */
	ctrl1_new = (ctrl1 & ~KXCJ9_RES_12BIT) | resolutions[index].reg;
	ret = raw_write8(accel_addr[id],  KXCJ9_CTRL1, ctrl1_new);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS) {
		sensor_resolution[id] = index;
		ctrl1 = ctrl1_new;
	}

	/* Re-enable the sensor. */
	if (enable_sensor(id, ctrl1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	return ret;
}

int accel_get_resolution(const enum accel_id id, int * const res)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	*res = resolutions[sensor_resolution[id]].val;
	return EC_SUCCESS;
}

int accel_set_datarate(const enum accel_id id, const int rate, const int rnd)
{
	int ret, ctrl1, index;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Find index for interface pair matching the specified rate. */
	index = find_param_index(rate, rnd, datarates, ARRAY_SIZE(datarates));

	/* Disable the sensor to allow for changing of critical parameters. */
	ret = disable_sensor(id, &ctrl1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Set output data rate. */
	ret = raw_write8(accel_addr[id],  KXCJ9_DATA_CTRL,
			datarates[index].reg);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		sensor_datarate[id] = index;

	/* Re-enable the sensor. */
	if (enable_sensor(id, ctrl1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	return ret;
}

int accel_get_datarate(const enum accel_id id, int * const rate)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	*rate = datarates[sensor_datarate[id]].val;
	return EC_SUCCESS;
}


#ifdef CONFIG_ACCEL_INTERRUPTS
int accel_set_interrupt(const enum accel_id id, unsigned int threshold)
{
	int ctrl1, tmp, ret;

	/* Disable the sensor to allow for changing of critical parameters. */
	ret = disable_sensor(id, &ctrl1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Set interrupt timer to 1 so it wakes up immediately. */
	ret = raw_write8(accel_addr[id], KXCJ9_WAKEUP_TIMER, 1);
	if (ret != EC_SUCCESS)
		goto error_enable_sensor;

	/*
	 * Set threshold, note threshold register is in units of 16 counts, so
	 * first we need to divide by 16 to get the value to send.
	 */
	threshold >>= 4;
	ret = raw_write8(accel_addr[id], KXCJ9_WAKEUP_THRESHOLD, threshold);
	if (ret != EC_SUCCESS)
		goto error_enable_sensor;

	/*
	 * Set interrupt enable register on sensor. Note that once this
	 * function is called once, the interrupt stays enabled and it is
	 * only necessary to clear KXCJ9_INT_REL to allow the next interrupt.
	 */
	ret = raw_read8(accel_addr[id], KXCJ9_INT_CTRL1, &tmp);
	if (ret != EC_SUCCESS)
		goto error_enable_sensor;
	if (!(tmp & KXCJ9_INT_CTRL1_IEN)) {
		ret = raw_write8(accel_addr[id], KXCJ9_INT_CTRL1,
				tmp | KXCJ9_INT_CTRL1_IEN);
		if (ret != EC_SUCCESS)
			goto error_enable_sensor;
	}

	/*
	 * Clear any pending interrupt on sensor by reading INT_REL register.
	 * Note: this register latches motion detected above threshold. Once
	 * latched, no interrupt can occur until this register is cleared.
	 */
	ret = raw_read8(accel_addr[id], KXCJ9_INT_REL, &tmp);

error_enable_sensor:
	/* Re-enable the sensor. */
	if (enable_sensor(id, ctrl1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	return ret;
}
#endif

int accel_read(const enum accel_id id, int * const x_acc, int * const y_acc,
		int * const z_acc)
{
	uint8_t acc[6];
	uint8_t reg = KXCJ9_XOUT_L;
	int ret, multiplier;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Read 6 bytes starting at KXCJ9_XOUT_L. */
	mutex_lock(&accel_mutex[id]);
	i2c_lock(I2C_PORT_ACCEL, 1);
	ret = i2c_xfer(I2C_PORT_ACCEL, accel_addr[id], &reg, 1, acc, 6,
			I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_ACCEL, 0);
	mutex_unlock(&accel_mutex[id]);

	if (ret != EC_SUCCESS)
		return ret;

	/* Determine multiplier based on stored range. */
	switch (ranges[sensor_range[id]].reg) {
	case KXCJ9_GSEL_2G:
		multiplier = 1;
		break;
	case KXCJ9_GSEL_4G:
		multiplier = 2;
		break;
	case KXCJ9_GSEL_8G:
	case KXCJ9_GSEL_8G_14BIT:
		multiplier = 4;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Convert acceleration to a signed 12-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = KXCJ9_XOUT_L
	 * acc[1] = KXCJ9_XOUT_H
	 * acc[2] = KXCJ9_YOUT_L
	 * acc[3] = KXCJ9_YOUT_H
	 * acc[4] = KXCJ9_ZOUT_L
	 * acc[5] = KXCJ9_ZOUT_H
	 */
	*x_acc = multiplier * (((int8_t)acc[1]) << 4) | (acc[0] >> 4);
	*y_acc = multiplier * (((int8_t)acc[3]) << 4) | (acc[2] >> 4);
	*z_acc = multiplier * (((int8_t)acc[5]) << 4) | (acc[4] >> 4);

	return EC_SUCCESS;
}

int accel_init(const enum accel_id id)
{
	int ret = EC_SUCCESS;
	int cnt = 0, ctrl1, ctrl2;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Disable the sensor to allow for changing of critical parameters. */
	ret = disable_sensor(id, &ctrl1);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 */
	ret = raw_write8(accel_addr[id], KXCJ9_CTRL2, KXCJ9_CTRL2_SRST);
	if (ret != EC_SUCCESS)
		return ret;

	/* Wait until software reset is complete or timeout. */
	while (1) {
		ret = raw_read8(accel_addr[id], KXCJ9_CTRL2, &ctrl2);

		/* Reset complete. */
		if (ret == EC_SUCCESS && !(ctrl2 & KXCJ9_CTRL2_SRST))
			break;

		/* Check for timeout. */
		if (cnt++ > 5)
			return EC_ERROR_TIMEOUT;

		/* Give more time for reset action to complete. */
		msleep(10);
	}

	/* Set resolution and range. */
	ctrl1 = resolutions[sensor_resolution[id]].reg |
			ranges[sensor_range[id]].reg;
#ifdef CONFIG_ACCEL_INTERRUPTS
	/* Enable wake up (motion detect) functionality. */
	ctrl1 |= KXCJ9_CTRL1_WUFE;
#endif
	ret = raw_write8(accel_addr[id], KXCJ9_CTRL1, ctrl1);

#ifdef CONFIG_ACCEL_INTERRUPTS
	/* Set interrupt polarity to rising edge and keep interrupt disabled. */
	ret |= raw_write8(accel_addr[id], KXCJ9_INT_CTRL1, KXCJ9_INT_CTRL1_IEA);

	/* Set output data rate for wake-up interrupt function. */
	ret |= raw_write8(accel_addr[id], KXCJ9_CTRL2, KXCJ9_OWUF_100_0HZ);

	/* Set interrupt to trigger on motion on any axis. */
	ret |= raw_write8(accel_addr[id], KXCJ9_INT_CTRL2,
			KXCJ9_INT_SRC2_XNWU | KXCJ9_INT_SRC2_XPWU |
			KXCJ9_INT_SRC2_YNWU | KXCJ9_INT_SRC2_YPWU |
			KXCJ9_INT_SRC2_ZNWU | KXCJ9_INT_SRC2_ZPWU);

	/*
	 * Enable accel interrupts. Note: accels will not initiate an interrupt
	 * until interrupt enable bit in KXCJ9_INT_CTRL1 is set on the device.
	 */
	gpio_enable_interrupt(GPIO_ACCEL_INT_LID);
	gpio_enable_interrupt(GPIO_ACCEL_INT_BASE);
#endif

	/* Set output data rate. */
	ret |= raw_write8(accel_addr[id], KXCJ9_DATA_CTRL,
			datarates[sensor_datarate[id]].reg);

	/* Enable the sensor. */
	ret |= enable_sensor(id, ctrl1);

	return ret;
}



/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_ACCELS
static int command_read_accelerometer(int argc, char **argv)
{
	char *e;
	int addr, reg, data;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is address. */
	addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	/* Second argument is register offset. */
	reg = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	raw_read8(addr, reg, &data);

	ccprintf("0x%02x\n", data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelread, command_read_accelerometer,
	"addr reg",
	"Read from accelerometer at slave address addr", NULL);

static int command_write_accelerometer(int argc, char **argv)
{
	char *e;
	int addr, reg, data;

	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is address. */
	addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	/* Second argument is register offset. */
	reg = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	/* Third argument is data. */
	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	raw_write8(addr, reg, data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelwrite, command_write_accelerometer,
	"addr reg data",
	"Write to accelerometer at slave address addr", NULL);

static int command_accelrange(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id > ACCEL_COUNT)
		return EC_ERROR_PARAM1;

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new range, if it returns invalid arg, then return
		 * a parameter error.
		 */
		if (accel_set_range(id, data, round) == EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		accel_get_range(id, &data);
		ccprintf("Range for sensor %d: %d\n", id, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrange, command_accelrange,
	"id [data [roundup]]",
	"Read or write accelerometer range", NULL);

static int command_accelresolution(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id > ACCEL_COUNT)
		return EC_ERROR_PARAM1;

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new resolution, if it returns invalid arg, then
		 * return a parameter error.
		 */
		if (accel_set_resolution(id, data, round) == EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		accel_get_resolution(id, &data);
		ccprintf("Resolution for sensor %d: %d\n", id, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelres, command_accelresolution,
	"id [data [roundup]]",
	"Read or write accelerometer resolution", NULL);

static int command_acceldatarate(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id > ACCEL_COUNT)
		return EC_ERROR_PARAM1;

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new data rate, if it returns invalid arg, then
		 * return a parameter error.
		 */
		if (accel_set_datarate(id, data, round) == EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		accel_get_datarate(id, &data);
		ccprintf("Data rate for sensor %d: %d\n", id, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrate, command_acceldatarate,
	"id [data [roundup]]",
	"Read or write accelerometer range", NULL);

#ifdef CONFIG_ACCEL_INTERRUPTS
static int command_accelerometer_interrupt(int argc, char **argv)
{
	char *e;
	int id, thresh;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_PARAM1;

	/* Second argument is interrupt threshold. */
	thresh = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	accel_set_interrupt(id, thresh);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelint, command_accelerometer_interrupt,
	"id threshold",
	"Write interrupt threshold", NULL);
#endif /* CONFIG_ACCEL_INTERRUPTS */

#endif /* CONFIG_CMD_ACCELS */
