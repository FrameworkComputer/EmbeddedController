/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Define typical operating power and max power */
#define OPERATING_POWER_MW 15000
#define MAX_POWER_MW       60000
#define MAX_CURRENT_MA     3000

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   900, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(5000, 20000, 15000),
		PDO_VAR(5000, 20000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Cap on the max voltage requested as a sink (in millivolts) */
static unsigned max_mv = -1; /* no cap */

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo,
		      uint32_t *curr_limit, uint32_t *supply_voltage)
{
	int i;
	int sel_mv;
	int max_uw = 0;
	int max_ma;
	int max_i = -1;
	int max;
	uint32_t flags;

	/* Get max power */
	for (i = 0; i < cnt; i++) {
		int uw;
		int mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
			uw = 250000 * (src_caps[i] & 0x3FF);
		} else {
			int ma = (src_caps[i] & 0x3FF) * 10;
			uw = ma * mv;
		}
		if ((uw > max_uw) && (mv <= max_mv)) {
			max_i = i;
			max_uw = uw;
			sel_mv = mv;
		}
	}
	if (max_i < 0)
		return -EC_ERROR_UNKNOWN;

	/* build rdo for desired power */
	if ((src_caps[max_i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		int uw = 250000 * (src_caps[max_i] & 0x3FF);
		max = MIN(1000 * uw, MAX_POWER_MW);
		flags = (max < OPERATING_POWER_MW) ? RDO_CAP_MISMATCH : 0;
		max_ma = 1000 * max / sel_mv;
		*rdo = RDO_BATT(max_i + 1, max, max, flags);
		CPRINTF("Request [%d] %dV %dmW",
			max_i, sel_mv/1000, max);
	} else {
		int ma = 10 * (src_caps[max_i] & 0x3FF);
		max = MIN(ma, MAX_CURRENT_MA);
		flags = (max * sel_mv) < (1000 * OPERATING_POWER_MW) ?
				RDO_CAP_MISMATCH : 0;
		max_ma = max;
		*rdo = RDO_FIXED(max_i + 1, max, max, flags);
		CPRINTF("Request [%d] %dV %dmA",
			max_i, sel_mv/1000, max);
	}
	/* Mismatch bit set if less power offered than the operating power */
	if (flags & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	*curr_limit = max_ma;
	*supply_voltage = sel_mv;
	return EC_SUCCESS;
}

void pd_set_max_voltage(unsigned mv)
{
	max_mv = mv;
}

int pd_request_voltage(uint32_t rdo)
{
	int op_ma = rdo & 0x3FF;
	int max_ma = (rdo >> 10) & 0x3FF;
	int idx = rdo >> 28;
	uint32_t pdo;
	uint32_t pdo_ma;

	if (!idx || idx > pd_src_pdo_cnt)
		return EC_ERROR_INVAL; /* Invalid index */

	/* check current ... */
	pdo = pd_src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);
	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */
	if (max_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much max current */

	CPRINTF("Switch to %d V %d mA (for %d/%d mA)\n",
		((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

	return EC_SUCCESS;
}

int pd_set_power_supply_ready(int port)
{
	/* Not implemented */
	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	/* Not implemented */
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* Not implemented */
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_power_swap(int port)
{
	/* Always allow power swap */
	return 1;
}

int pd_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

int pd_custom_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	return 0;
}
