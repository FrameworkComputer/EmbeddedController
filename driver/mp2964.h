/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PMIC_MP2964_H
#define __CROS_EC_PMIC_MP2964_H

#define MP2964_PAGE			0x00
#define MP2964_STORE_USER_ALL		0x15
#define MP2964_RESTORE_USER_ALL		0x16
#define MP2964_MFR_VOUT_TRIM		0x22
#define MP2964_MFR_PHASE_NUM		0x29
#define MP2964_MFR_IMON_SNS_OFFS	0x2c
#define MP2964_IOUT_CAL_GAIN_SET	0x38
#define MP2964_MFR_TRANS_FAST		0x3d
#define MP2964_MFR_ALT_SET		0x3f
#define MP2964_MFR_CONFIG2		0x48
#define MP2964_MFR_SLOPE_SR_DCM		0x4e
#define MP2964_MFR_ICC_MAX_SET		0x53
#define MP2964_MFR_OCP_OVP_DAC_LIMIT	0x60
#define MP2964_MFR_OCP_SET		0x62
#define MP2964_PRODUCT_DATA_CODE	0x93
#define MP2964_LOT_CODE_VR		0x94
#define MP2964_MFR_PSI_TRIM4		0xb0
#define MP2964_MFR_PSI_TRIM1		0xb1
#define MP2964_MFR_PSI_TRIM3		0xb3
#define MP2964_MFR_SLOPE_CNT_2P		0xd4
#define MP2964_MFR_SLOPE_CNT_5P		0xe0
#define MP2964_MFR_IMON_SVID1		0xe8
#define MP2964_MFR_IMON_SVID2		0xe9
#define MP2964_MFR_IMON_SVID3		0xea
#define MP2964_MFR_IMON_SVID4		0xeb
#define MP2964_MFR_IMON_SVID5		0xef
#define MP2964_MFR_IMON_SVID6		0xf0

struct mp2964_reg_val {
	uint8_t reg;
	uint16_t val;
};

int mp2964_tune(const struct mp2964_reg_val *page0, int count0,
		const struct mp2964_reg_val *page1, int count1);

#endif /* __CROS_EC_PMIC_MP2964_H */
