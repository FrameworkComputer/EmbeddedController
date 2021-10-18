/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "compile_time_macros.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_pd_pdo.h"

#define CHG_PDO_FIXED_FLAGS (PDO_FIXED_DATA_SWAP)

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, CHG_PDO_FIXED_FLAGS),
		PDO_BATT(4750, 21000, 15000),
		PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/*
 * Dynamic PDO that reflects capabilities present on the CHG port. Allow for
 * multiple entries so that we can offer greater than 5V charging. The 1st
 * entry will be fixed 5V, but its current value may change based on the CHG
 * port vbus info. Subsequent entries are used for when offering vbus greater
 * than 5V.
 */
const uint16_t pd_src_voltages_mv[] = {
	5000, 9000, 10000, 12000, 15000, 20000,
};
uint32_t pd_src_chg_pdo[ARRAY_SIZE(pd_src_voltages_mv)];
uint8_t chg_pdo_cnt;

int active_charge_port = CHARGE_PORT_NONE;
struct vbus_prop vbus[CONFIG_USB_PD_PORT_MAX_COUNT];

int charge_port_is_active(void)
{
	return active_charge_port == CHG && vbus[CHG].mv > 0;
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	int pdo_cnt = 0;

	/*
	 * If CHG is providing VBUS, then advertise what's available on the CHG
	 * port, otherwise we provide no power.
	 */
	if (charge_port_is_active()) {
		*src_pdo =  pd_src_chg_pdo;
		pdo_cnt = chg_pdo_cnt;
	}

	return pdo_cnt;
}
