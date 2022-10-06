/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "usb_pd.h"

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
	PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
	PDO_BATT(4750, PD_MAX_VOLTAGE_MV, PD_OPERATING_POWER_MW),
	PDO_VAR(4750, PD_MAX_VOLTAGE_MV, PD_MAX_CURRENT_MA),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);
#endif /* CONFIG_USB_PD_CUSTOM_PDO */

uint8_t board_get_usb_pd_port_count(void)
{
	return 1;
}
