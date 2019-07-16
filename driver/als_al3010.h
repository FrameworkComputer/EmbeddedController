/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dyna-Image AL3010 light sensor driver
 */

#ifndef __CROS_EC_ALS_AL3010_H
#define __CROS_EC_ALS_AL3010_H

/* I2C interface */
#define AL3010_I2C_ADDR1_FLAGS		0x1C
#define AL3010_I2C_ADDR2_FLAGS		0x1D
#define AL3010_I2C_ADDR3_FLAGS		0x1E

/* AL3010 registers */
#define AL3010_REG_SYSTEM		0x00
#define AL3010_REG_INT_STATUS		0x01
#define AL3010_REG_CONFIG		0x10
#define AL3010_REG_DATA_LOW		0x0C

#define AL3010_ENABLE	0x01
#define AL3010_GAIN_SELECT 3

#define	AL3010_GAIN_1 0 /* 77806 lx */
#define	AL3010_GAIN_2 1 /* 19452 lx  */
#define	AL3010_GAIN_3 2 /* 4863  lx  */
#define	AL3010_GAIN_4 3 /* 1216  lx  */
#define AL3010_GAIN	CONCAT2(AL3010_GAIN_, AL3010_GAIN_SELECT)

#define AL3010_GAIN_SCALE_1 11872	/* 1.1872 lux/count */
#define AL3010_GAIN_SCALE_2 2968	/* 0.2968 lux/count */
#define AL3010_GAIN_SCALE_3 742		/* 0.0742 lux/count */
#define AL3010_GAIN_SCALE_4 186		/* 0.0186 lux/count */
#define AL3010_GAIN_SCALE CONCAT2(AL3010_GAIN_SCALE_, AL3010_GAIN_SELECT)

int al3010_init(void);
int al3010_read_lux(int *lux, int af);

#endif	/* __CROS_EC_ALS_AL3010_H */
