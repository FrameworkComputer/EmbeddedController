/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* KXCJ9 gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_KXCJ9_H
#define __CROS_EC_ACCEL_KXCJ9_H

/*
 * 7-bit address is 000111Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define KXCJ9_ADDR0		0x1c
#define KXCJ9_ADDR1		0x1e

/* Chip-specific registers */
#define KXCJ9_XOUT_L		0x06
#define KXCJ9_XOUT_H		0x07
#define KXCJ9_YOUT_L		0x08
#define KXCJ9_YOUT_H		0x09
#define KXCJ9_ZOUT_L		0x0a
#define KXCJ9_ZOUT_H		0x0b
#define KXCJ9_DCST_RESP		0x0c
#define KXCJ9_WHOAMI		0x0f
#define KXCJ9_INT_SRC1		0x16
#define KXCJ9_INT_SRC2		0x17
#define KXCJ9_STATUS		0x18
#define KXCJ9_INT_REL		0x1a
#define KXCJ9_CTRL1		0x1b
#define KXCJ9_CTRL2		0x1d
#define KXCJ9_INT_CTRL1		0x1e
#define KXCJ9_INT_CTRL2		0x1f
#define KXCJ9_DATA_CTRL		0x21
#define KXCJ9_WAKEUP_TIMER	0x29
#define KXCJ9_SELF_TEST		0x3a
#define KXCJ9_WAKEUP_THRESHOLD	0x6a

#define KXCJ9_INT_SRC1_WUFS	(1 << 1)
#define KXCJ9_INT_SRC1_DRDY	(1 << 4)

#define KXCJ9_INT_SRC2_ZPWU	(1 << 0)
#define KXCJ9_INT_SRC2_ZNWU	(1 << 1)
#define KXCJ9_INT_SRC2_YPWU	(1 << 2)
#define KXCJ9_INT_SRC2_YNWU	(1 << 3)
#define KXCJ9_INT_SRC2_XPWU	(1 << 4)
#define KXCJ9_INT_SRC2_XNWU	(1 << 5)

#define KXCJ9_STATUS_INT	(1 << 4)

#define KXCJ9_CTRL1_WUFE	(1 << 1)
#define KXCJ9_CTRL1_DRDYE	(1 << 5)
#define KXCJ9_CTRL1_PC1		(1 << 7)

#define KXCJ9_GSEL_2G		(0 << 3)
#define KXCJ9_GSEL_4G		(1 << 3)
#define KXCJ9_GSEL_8G		(2 << 3)
#define KXCJ9_GSEL_8G_14BIT	(3 << 3)

#define KXCJ9_RES_8BIT		(0 << 6)
#define KXCJ9_RES_12BIT		(1 << 6)

#define KXCJ9_CTRL2_OWUF	(7 << 0)
#define KXCJ9_CTRL2_DCST	(1 << 4)
#define KXCJ9_CTRL2_SRST	(1 << 7)

#define KXCJ9_OWUF_0_781HZ	0
#define KXCJ9_OWUF_1_563HZ	1
#define KXCJ9_OWUF_3_125HZ	2
#define KXCJ9_OWUF_6_250HZ	3
#define KXCJ9_OWUF_12_50HZ	4
#define KXCJ9_OWUF_25_00HZ	5
#define KXCJ9_OWUF_50_00HZ	6
#define KXCJ9_OWUF_100_0HZ	7

#define KXCJ9_INT_CTRL1_IEL		(1 << 3)
#define KXCJ9_INT_CTRL1_IEA		(1 << 4)
#define KXCJ9_INT_CTRL1_IEN		(1 << 5)

#define KXCJ9_INT_CTRL2_ZPWUE		(1 << 0)
#define KXCJ9_INT_CTRL2_ZNWUE		(1 << 1)
#define KXCJ9_INT_CTRL2_YPWUE		(1 << 2)
#define KXCJ9_INT_CTRL2_YNWUE		(1 << 3)
#define KXCJ9_INT_CTRL2_XPWUE		(1 << 4)
#define KXCJ9_INT_CTRL2_XNWUE		(1 << 5)

#define KXCJ9_OSA_0_781HZ	8
#define KXCJ9_OSA_1_563HZ	9
#define KXCJ9_OSA_3_125HZ	0xa
#define KXCJ9_OSA_6_250HZ	0xb
#define KXCJ9_OSA_12_50HZ	0
#define KXCJ9_OSA_25_00HZ	1
#define KXCJ9_OSA_50_00HZ	2
#define KXCJ9_OSA_100_0HZ	3
#define KXCJ9_OSA_200_0HZ	4
#define KXCJ9_OSA_400_0HZ	5
#define KXCJ9_OSA_800_0HZ	6
#define KXCJ9_OSA_1600_HZ	7


/**
 * Write the accelerometer range.
 *
 * @param id Target accelerometer
 * @param range Range (KXCJ9_GSEL_*).
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_write_range(const enum accel_id id, const int range);

/**
 * Write the accelerometer resolution.
 *
 * @param id Target accelerometer
 * @param range Resolution (KXCJ9_RES_*).
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_write_resolution(const enum accel_id id, const int res);

/**
 * Write the accelerometer data rate.
 *
 * @param id Target accelerometer
 * @param range Data rate (KXCJ9_OSA_*).
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_write_datarate(const enum accel_id id, const int rate);

#endif /* __CROS_EC_ACCEL_KXCJ9_H */
