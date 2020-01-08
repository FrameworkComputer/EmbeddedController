/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI OPT3001 light sensor driver
 */

#ifndef __CROS_EC_ALS_OPT3001_H
#define __CROS_EC_ALS_OPT3001_H

/* I2C interface */
#define OPT3001_I2C_ADDR1_FLAGS		0x44
#define OPT3001_I2C_ADDR2_FLAGS		0x45
#define OPT3001_I2C_ADDR3_FLAGS		0x46
#define OPT3001_I2C_ADDR4_FLAGS		0x47

/* OPT3001 registers */
#define OPT3001_REG_RESULT		0x00
#define OPT3001_REG_CONFIGURE		0x01
#define OPT3001_RANGE_OFFSET			12
#define OPT3001_RANGE_MASK			0x0fff
#define OPT3001_MODE_OFFSET			9
#define OPT3001_MODE_MASK			0xf9ff
enum opt3001_mode {
	OPT3001_MODE_SUSPEND,
	OPT3001_MODE_FORCED,
	OPT3001_MODE_CONTINUOUS,
};

#define OPT3001_REG_INT_LIMIT_LSB	0x02
#define OPT3001_REG_INT_LIMIT_MSB	0x03
#define OPT3001_REG_MAN_ID		0x7e
#define OPT3001_REG_DEV_ID		0x7f

/* OPT3001 register values */
#define OPT3001_MANUFACTURER_ID		0x5449
#define OPT3001_DEVICE_ID		0x3001

/*
 * Min and Max sampling frequency in mHz.
 * Due to integration set at 800ms, we limit max frequency to 1Hz.
 */
#define OPT3001_LIGHT_MIN_FREQ          100
#define OPT3001_LIGHT_MAX_FREQ          1000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= OPT3001_LIGHT_MAX_FREQ)
#error "EC too slow for light sensor"
#endif

#ifdef HAS_TASK_ALS
int opt3001_init(void);
int opt3001_read_lux(int *lux, int af);
#else
#define OPT3001_GET_DATA(_s)	((struct opt3001_drv_data_t *)(_s)->drv_data)

struct opt3001_drv_data_t {
	int rate;
	int last_value;
	/* the coef is scale.uscale */
	int16_t scale;
	uint16_t uscale;
	int16_t offset;
};

extern const struct accelgyro_drv opt3001_drv;
#endif

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ALS
extern struct i2c_stress_test_dev opt3001_i2c_stress_test_dev;
#endif

#endif	/* __CROS_EC_ALS_OPT3001_H */
