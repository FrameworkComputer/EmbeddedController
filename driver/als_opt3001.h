/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI OPT3001 light sensor driver
 */

#ifndef __CROS_EC_ALS_OPT3001_H
#define __CROS_EC_ALS_OPT3001_H

/* I2C interface */
#define OPT3001_I2C_ADDR1		(0x44 << 1)
#define OPT3001_I2C_ADDR2		(0x45 << 1)
#define OPT3001_I2C_ADDR3		(0x46 << 1)
#define OPT3001_I2C_ADDR4		(0x47 << 1)

/* OPT3001 registers */
#define OPT3001_REG_RESULT		0x00
#define OPT3001_REG_CONFIGURE		0x01
#define OPT3001_REG_INT_LIMIT_LSB	0x02
#define OPT3001_REG_INT_LIMIT_MSB	0x03
#define OPT3001_REG_MAN_ID		0x7E
#define OPT3001_REG_DEV_ID		0x7F

/* OPT3001 register values */
#define OPT3001_MANUFACTURER_ID		0x5449
#define OPT3001_DEVICE_ID		0x3001

int opt3001_init(void);
int opt3001_read_lux(int *lux, int af);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ALS
extern struct i2c_stress_test_dev opt3001_i2c_stress_test_dev;
#endif

#endif	/* __CROS_EC_ALS_OPT3001_H */
