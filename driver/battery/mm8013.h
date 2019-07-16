/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for MM8013.
 */

#ifndef __CROS_EC_MM8013_H
#define __CROS_EC_MM8013_H

#define MM8013_ADDR_FLAGS           0x55

#define REG_TEMPERATURE             0x06
#define REG_VOLTAGE                 0x08
#define REG_FLAGS                   0x0a
#define REG_FULL_CHARGE_CAPACITY    0x0e
#define REG_REMAINING_CAPACITY      0x10
#define REG_AVERAGE_CURRENT         0x14
#define REG_AVERAGE_TIME_TO_EMPTY   0x16
#define REG_AVERAGE_TIME_TO_FULL    0x18
#define REG_STATE_OF_CHARGE         0x2c
#define REG_CYCLE_COUNT             0x2a
#define REG_DESIGN_CAPACITY         0x3c
#define REG_PRODUCT_INFORMATION     0x64

/* Over Temperature in charge */
#define MM8013_FLAG_OTC             BIT(15)
/* Over Temperature in discharge */
#define MM8013_FLAG_OTD             BIT(14)
/* Over-charge */
#define MM8013_FLAG_BATHI           BIT(13)
/* Full Charge */
#define MM8013_FLAG_FC              BIT(9)
/* Charge allowed */
#define MM8013_FLAG_CHG             BIT(8)
/* Discharge */
#define MM8013_FLAG_DSG             BIT(0)


#endif /* __CROS_EC_MM8013_H */
