/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI SN5S330 Type-C Power Path Controller */

#ifndef __CROS_EC_SN5S330_H
#define __CROS_EC_SN5S330_H

#include "common.h"

struct sn5s330_config {
	uint8_t i2c_port;
	uint8_t i2c_addr;
};

extern const struct sn5s330_config sn5s330_chips[];
extern const unsigned int sn5s330_cnt;

/* Power Path Indices */
enum sn5s330_pp_idx {
	SN5S330_PP1,
	SN5S330_PP2,
	SN5S330_PP_COUNT,
};

#define SN5S330_ADDR0 0x80
#define SN5S330_ADDR1 0x82
#define SN5S330_ADDR2 0x84
#define SN5S330_ADDR3 0x86

#define SN5S330_FUNC_SET1  0x50
#define SN5S330_FUNC_SET2  0x51
#define SN5S330_FUNC_SET3  0x52
#define SN5S330_FUNC_SET4  0x53
#define SN5S330_FUNC_SET5  0x54
#define SN5S330_FUNC_SET6  0x55
#define SN5S330_FUNC_SET7  0x56
#define SN5S330_FUNC_SET8  0x57
#define SN5S330_FUNC_SET9  0x58
#define SN5S330_FUNC_SET10 0x59
#define SN5S330_FUNC_SET11 0x5A
#define SN5S330_FUNC_SET12 0x5B

#define SN5S330_INT_STATUS_REG1 0x2F
#define SN5S330_INT_STATUS_REG2 0x30
#define SN5S330_INT_STATUS_REG3 0x31
#define SN5S330_INT_STATUS_REG4 0x32

#define SN5S330_INT_TRIP_RISE_REG1 0x20
#define SN5S330_INT_TRIP_RISE_REG2 0x21
#define SN5S330_INT_TRIP_RISE_REG3 0x22
#define SN5S330_INT_TRIP_FALL_REG1 0x23
#define SN5S330_INT_TRIP_FALL_REG2 0x24
#define SN5S330_INT_TRIP_FALL_REG3 0x25

#define SN5S330_INT_MASK_RISE_REG1 0x26
#define SN5S330_INT_MASK_RISE_REG2 0x27
#define SN5S330_INT_MASK_RISE_REG3 0x28
#define SN5S330_INT_MASK_FALL_REG1 0x29
#define SN5S330_INT_MASK_FALL_REG2 0x2A
#define SN5S330_INT_MASK_FALL_REG3 0x2B

#define PPX_ILIM_DEGLITCH_0_US_20     0x1
#define PPX_ILIM_DEGLITCH_0_US_50     0x2
#define PPX_ILIM_DEGLITCH_0_US_100    0x3
#define PPX_ILIM_DEGLITCH_0_US_200    0x4
#define PPX_ILIM_DEGLITCH_0_US_1000   0x5
#define PPX_ILIM_DEGLITCH_0_US_2000   0x6
#define PPX_ILIM_DEGLITCH_0_US_10000  0x7

/* Internal VBUS Switch Current Limit Settings (min) */
#define SN5S330_ILIM_0_35  0
#define SN5S330_ILIM_0_63  1
#define SN5S330_ILIM_0_90  2
#define SN5S330_ILIM_1_14  3
#define SN5S330_ILIM_1_38  4
#define SN5S330_ILIM_1_62  5
#define SN5S330_ILIM_1_86  6
#define SN5S330_ILIM_2_10  7
#define SN5S330_ILIM_2_34  8
#define SN5S330_ILIM_2_58  9
#define SN5S330_ILIM_2_82  10
#define SN5S330_ILIM_3_06  11
#define SN5S330_ILIM_3_30  12

/* FUNC_SET_2 */
#define SN5S330_SBU_EN (1 << 4)

/* FUNC_SET_3 */
#define SN5S330_PP1_EN (1 << 0)
#define SN5S330_PP2_EN (1 << 1)
#define SN5S330_SET_RCP_MODE_PP1 (1 << 5)
#define SN5S330_SET_RCP_MODE_PP2 (1 << 6)

#define SN5S330_CC_EN (1 << 4)

/* FUNC_SET_9 */
#define SN5S330_PP2_CONFIG (1 << 2)
#define SN5S330_OVP_EN_CC (1 << 4)

/* INT_STATUS_REG4 */
#define SN5S330_DIG_RES (1 << 0)
#define SN5S330_DB_BOOT (1 << 1)
#define SN5S330_VSAFE0V_STAT (1 << 2)
#define SN5S330_VSAFE0V_MASK (1 << 3)

/*
 * INT_MASK_RISE_EDGE_1
 *
 * The ILIM_PP1 bit indicates an overcurrent condition when sourcing on power
 * path 1.  For rising edge registers, this indicates an overcurrent has
 * occured; similarly for falling edge, it means the overcurrent condition is no
 * longer present.
 */
#define SN5S330_ILIM_PP1_RISE_MASK (1 << 4)

/* INT_MASK_FALL_EDGE_1 */
#define SN5S330_ILIM_PP1_FALL_MASK (1 << 4)

/**
 * Turn on/off the PP1 or PP2 FET.
 *
 * @param chip_idx: The index into the sn5s330_chips[] table.
 * @param pp: The power path index (PP1 or PP2).
 * @param enable: 1 to turn on the FET, 0 to turn off.
 * @return EC_SUCCESS on success,
 *         otherwise if failed to enable the FET.
 */
int sn5s330_pp_fet_enable(uint8_t chip_idx, enum sn5s330_pp_idx pp, int enable);

#endif /* defined(__CROS_EC_SN5S330_H) */
