/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMM150 magnetometer definition */

#ifndef __CROS_EC_MAG_BMM150_H
#define __CROS_EC_MAG_BMM150_H

#include "accelgyro.h"

#define BMM150_ADDR0             0x20
#define BMM150_ADDR1             0x22
#define BMM150_ADDR2             0x24
#define BMM150_ADDR3             0x26

#define BMM150_CHIP_ID           0x40
#define BMM150_CHIP_ID_MAJOR     0x32

#define BMM150_BASE_DATA         0x42

#define BMM150_INT_STATUS        0x4a
#define BMM150_PWR_CTRL          0x4b
#define BMM150_SRST                  ((1 << 7) | (1 << 1))
#define BMM150_PWR_ON                (1 << 0)

#define BMM150_OP_CTRL           0x4c
#define BMM150_OP_MODE_OFFSET    1
#define BMM150_OP_MODE_MASK      3
#define BMM150_OP_MODE_NORMAL    0x00
#define BMM150_OP_MODE_FORCED    0x01
#define BMM150_OP_MODE_SLEEP     0x03

#define BMM150_INT_CTRL          0x4d

#endif /* __CROS_EC_MAG_BMM150_H */
