/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START - TCPMv1 is difficult to meaningfully test: b/304349098. */

#include "usb_pd.h"
#include "usb_pd_pdo.h"
#include "util.h"

#ifndef CONFIG_USB_PD_CUSTOM_PDO

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
const uint32_t pd_src_pdo_max[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_max_cnt = ARRAY_SIZE(pd_src_pdo_max);

const uint32_t pd_snk_pdo[] = {
	PDO_FIXED(5000,
		  GENERIC_MIN((PD_OPERATING_POWER_MW / 5), PD_MAX_CURRENT_MA),
		  PDO_FIXED_FLAGS),
	PDO_BATT(4750, PD_MAX_VOLTAGE_MV, PD_OPERATING_POWER_MW),
	PDO_VAR(4750, PD_MAX_VOLTAGE_MV, PD_MAX_CURRENT_MA),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

#endif /* CONFIG_USB_PD_CUSTOM_PDO */

/* LCOV_EXCL_END */
