/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* KX022 gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_KX022_H
#define __CROS_EC_ACCEL_KX022_H

/*
 * 7-bit address is 001111Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define KX022_ADDR0		0x3c
#define KX022_ADDR1		0x3e

/* Chip-specific registers */
#define KX022_XHP_L		0x00
#define KX022_XHP_H		0x01
#define KX022_YHP_L		0x02
#define KX022_YHP_H		0x03
#define KX022_ZHP_L		0x04
#define KX022_ZHP_H		0x05
#define KX022_XOUT_L		0x06
#define KX022_XOUT_H		0x07
#define KX022_YOUT_L		0x08
#define KX022_YOUT_H		0x09
#define KX022_ZOUT_L		0x0a
#define KX022_ZOUT_H		0x0b
#define KX022_COTR		0x0c
#define KX022_WHOAMI		0x0f
#define KX022_TSCP		0x10
#define KX022_TSPP		0x11
#define KX022_INS1		0x12
#define KX022_INS2		0x13
#define KX022_INS3		0x14
#define KX022_STATUS_REG	0x15
#define KX022_INT_REL		0x17
#define KX022_CNTL1		0x18
#define KX022_CNTL2		0x19
#define KX022_CNTL3		0x1a
#define KX022_ODCNTL		0x1b
#define KX022_INC1		0x1c
#define KX022_INC2		0x1d
#define KX022_INC3		0x1e
#define KX022_INC4		0x1f
#define KX022_INC5		0x20
#define KX022_INC6		0x21
#define KX022_TILT_TIMER	0x22
#define KX022_WUFC		0x23
#define KX022_TDTRC		0x24
#define KX022_TDTC		0x25
#define KX022_TTH		0x26
#define KX022_TTL		0x27
#define KX022_FTD		0x28
#define KX022_STD		0x29
#define KX022_TLT		0x2a
#define KX022_TWS		0x2b
#define KX022_ATH		0x30
#define KX022_TILT_ANGLE_LL	0x32
#define KX022_TILT_ANGLE_HL	0x33
#define KX022_HYST_SET		0x34
#define KX022_LP_CNTL		0x35
#define KX022_BUF_CNTL1	0x3a
#define KX022_BUF_CNTL2	0x3b
#define KX022_BUF_STATUS_1	0x3c
#define KX022_BUF_STATUS_2	0x3d
#define KX022_BUF_CLEAR	0x3e
#define KX022_BUF_READ		0x3f
#define KX022_SELF_TEST	0x60


#define KX022_CNTL1_PC1		(1 << 7)
#define KX022_CNTL1_WUFE	(1 << 1)

#define KX022_CNTL2_SRST	(1 << 7)

#define KX022_CNTL3_OWUF_FIELD	7

#define KX022_INC1_IEA		(1 << 4)
#define KX022_INC1_IEN		(1 << 5)

#define KX022_GSEL_2G		(0 << 3)
#define KX022_GSEL_4G		(1 << 3)
#define KX022_GSEL_8G		(2 << 3)
#define KX022_GSEL_FIELD	(3 << 3)

#define KX022_RES_8BIT		(0 << 6)
#define KX022_RES_16BIT		(1 << 6)

#define KX022_OSA_0_781HZ	8
#define KX022_OSA_1_563HZ	9
#define KX022_OSA_3_125HZ	0xa
#define KX022_OSA_6_250HZ	0xb
#define KX022_OSA_12_50HZ	0
#define KX022_OSA_25_00HZ	1
#define KX022_OSA_50_00HZ	2
#define KX022_OSA_100_0HZ	3
#define KX022_OSA_200_0HZ	4
#define KX022_OSA_400_0HZ	5
#define KX022_OSA_800_0HZ	6
#define KX022_OSA_1600HZ	7
#define KX022_OSA_FIELD		0xf

#define KX022_OWUF_0_781HZ	0
#define KX022_OWUF_1_563HZ	1
#define KX022_OWUF_3_125HZ	2
#define KX022_OWUF_6_250HZ	3
#define KX022_OWUF_12_50HZ	4
#define KX022_OWUF_25_00HZ	5
#define KX022_OWUF_50_00HZ	6
#define KX022_OWUF_100_0HZ	7

#define KX022_INC2_ZPWUE	(1 << 0)
#define KX022_INC2_ZNWUE	(1 << 1)
#define KX022_INC2_YPWUE	(1 << 2)
#define KX022_INC2_YNWUE	(1 << 3)
#define KX022_INC2_XPWUE	(1 << 4)
#define KX022_INC2_XNWUE	(1 << 5)

#endif /* __CROS_EC_ACCEL_KX022_H */
