/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Vishay CM36781 light sensor driver
 */

#ifndef __CROS_EC_ALS_CM36781_H
#define __CROS_EC_ALS_CM36781_H

/* I2C interface */
#define CM36781_I2C_ADDR_FLAGS 0x51

// CM36781 registers
#define CM36781_ALS_CONF 0x00
#define CM36781_ALS_THDH 0x01
#define CM36781_ALS_THDL 0x02
#define CM36781_ALS_DATA 0x09
#define CM36781_WHITE_DATA 0x0A
#define CM36781_ID 0x0E

// CM36781 ALS_CONF register bits
#define CM36781_ALS_SD 0x0001
#define CM36781_ALS_INT_EN 0x0002
#define CM36781_ALS_PERS_1 0x0000
#define CM36781_ALS_PERS_2 0x0004
#define CM36781_ALS_PERS_4 0x0008
#define CM36781_ALS_PERS_8 0x000C
#define CM36781_ALS_IT_MASK 0x00C0
#define CM36781_ALS_IT_50MS 0x0000
#define CM36781_ALS_IT_100MS 0x0040
#define CM36781_ALS_IT_200MS 0x0080
#define CM36781_ALS_IT_400MS 0x00C0
#define CM36781_ALS_CONF_DEFAULT (CM36781_ALS_IT_50MS)

// CM36781 device ID
#define CM36781_DEV_ID_MASK 0xFF
#define CM36781_DEV_ID 0x58

/* Range from 1 to 10 Hz */
#define CM36781_MAX_FREQ (10 * 1000)
#define CM36781_MIN_FREQ (1 * 1000)

extern const struct accelgyro_drv cm36781_drv;

#endif /* __CROS_EC_ALS_CM36781_H */
