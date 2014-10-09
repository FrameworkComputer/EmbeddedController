/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Source PDOs */
const uint32_t pd_src_pdo[] = {};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000,   500, 0),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Desired voltage requested as a sink (in millivolts) */
static unsigned select_mv = 5000;

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo)
{
	int i;
	int ma;
	int set_mv = select_mv;

	/* Default to 5V */
	if (set_mv <= 0)
		set_mv = 5000;

	/* Get the selected voltage */
	for (i = cnt; i >= 0; i--) {
		int mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		int type = src_caps[i] & PDO_TYPE_MASK;
		if ((mv == set_mv) && (type == PDO_TYPE_FIXED))
			break;
	}
	if (i < 0)
		return -EC_ERROR_UNKNOWN;

	/* request all the power ... */
	ma = 10 * (src_caps[i] & 0x3FF);
	*rdo = RDO_FIXED(i + 1, ma, ma, 0);
	ccprintf("Request [%d] %dV %dmA\n", i, set_mv/1000, ma);
	return ma;
}

void pd_set_input_current_limit(int port, uint32_t max_ma)
{
	/* No battery, nothing to do */
	return;
}

void pd_set_max_voltage(unsigned mv)
{
	select_mv = mv;
}

int requested_voltage_idx;
int pd_request_voltage(uint32_t rdo)
{
	return EC_SUCCESS;
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

