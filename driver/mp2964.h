/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PMIC_MP2964_H
#define __CROS_EC_PMIC_MP2964_H

#define MP2964_PAGE			0x00
#define MP2964_STORE_USER_ALL		0x15
#define MP2964_RESTORE_USER_ALL		0x16
#define MP2964_MFR_ALT_SET		0x3f

struct mp2964_reg_val {
	uint8_t reg;
	uint16_t val;
};

int mp2964_tune(const struct mp2964_reg_val *page0, int count0,
		const struct mp2964_reg_val *page1, int count1);

#endif /* __CROS_EC_PMIC_MP2964_H */
