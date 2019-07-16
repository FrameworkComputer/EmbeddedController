/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* KXCJ9 gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_KXCJ9_H
#define __CROS_EC_ACCEL_KXCJ9_H

#include "task.h"

/*
 * 7-bit address is 000111Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define KXCJ9_ADDR0_FLAGS	0x0E
#define KXCJ9_ADDR1_FLAGS	0x0D
#define KXCJ9_WHO_AM_I_VAL	0x0A

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

#define KXCJ9_INT_SRC1_WUFS	BIT(1)
#define KXCJ9_INT_SRC1_DRDY	BIT(4)

#define KXCJ9_INT_SRC2_ZPWU	BIT(0)
#define KXCJ9_INT_SRC2_ZNWU	BIT(1)
#define KXCJ9_INT_SRC2_YPWU	BIT(2)
#define KXCJ9_INT_SRC2_YNWU	BIT(3)
#define KXCJ9_INT_SRC2_XPWU	BIT(4)
#define KXCJ9_INT_SRC2_XNWU	BIT(5)

#define KXCJ9_STATUS_INT	BIT(4)

#define KXCJ9_CTRL1_WUFE	BIT(1)
#define KXCJ9_CTRL1_DRDYE	BIT(5)
#define KXCJ9_CTRL1_PC1		BIT(7)

#define KXCJ9_GSEL_2G		(0 << 3)
#define KXCJ9_GSEL_4G		BIT(3)
#define KXCJ9_GSEL_8G		(2 << 3)
#define KXCJ9_GSEL_8G_14BIT	(3 << 3)
#define KXCJ9_GSEL_ALL          (3 << 3)

#define KXCJ9_RES_8BIT		(0 << 6)
#define KXCJ9_RES_12BIT		BIT(6)

#define KXCJ9_CTRL2_OWUF	(7 << 0)
#define KXCJ9_CTRL2_DCST	BIT(4)
#define KXCJ9_CTRL2_SRST	BIT(7)

#define KXCJ9_OWUF_0_781HZ	0
#define KXCJ9_OWUF_1_563HZ	1
#define KXCJ9_OWUF_3_125HZ	2
#define KXCJ9_OWUF_6_250HZ	3
#define KXCJ9_OWUF_12_50HZ	4
#define KXCJ9_OWUF_25_00HZ	5
#define KXCJ9_OWUF_50_00HZ	6
#define KXCJ9_OWUF_100_0HZ	7

#define KXCJ9_INT_CTRL1_IEL		BIT(3)
#define KXCJ9_INT_CTRL1_IEA		BIT(4)
#define KXCJ9_INT_CTRL1_IEN		BIT(5)

#define KXCJ9_INT_CTRL2_ZPWUE		BIT(0)
#define KXCJ9_INT_CTRL2_ZNWUE		BIT(1)
#define KXCJ9_INT_CTRL2_YPWUE		BIT(2)
#define KXCJ9_INT_CTRL2_YNWUE		BIT(3)
#define KXCJ9_INT_CTRL2_XPWUE		BIT(4)
#define KXCJ9_INT_CTRL2_XNWUE		BIT(5)

#define KXCJ9_OSA_0_000HZ	0
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
#define KXCJ9_OSA_FIELD		0xf

/* Min and Max sampling frequency in mHz */
#define KXCJ9_ACCEL_MIN_FREQ    12500
#define KXCJ9_ACCEL_MAX_FREQ    MOTION_MAX_SENSOR_FREQUENCY(1600000, 6250)

#endif /* __CROS_EC_ACCEL_KXCJ9_H */
