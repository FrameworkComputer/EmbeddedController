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

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Acceptable margin between requested VBUS and measured value */
#define MARGIN_MV 400 /* mV */

/* Define typical operating power and max power */
#define OPERATING_POWER_MW 5000
#define MAX_POWER_MW       60000
#define MAX_CURRENT_MA     3000

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | \
			 PDO_FIXED_EXTERNAL)

/* Source PDOs */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
};
static const int pd_src_pdo_cnts[3] = {
		[SRC_CAP_5V] = 1,
		[SRC_CAP_12V] = 2,
		[SRC_CAP_20V] = 3,
};

static int pd_src_pdo_idx;

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_FLAGS),
		PDO_FIXED(12000,  500, PDO_FIXED_FLAGS),
		PDO_FIXED(20000,  500, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Desired voltage requested as a sink (in millivolts) */
static unsigned select_mv = 5000;

void board_set_source_cap(enum board_src_cap cap)
{
	pd_src_pdo_idx = cap;
}

int pd_get_source_pdo(const uint32_t **src_pdo)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnts[pd_src_pdo_idx];
}

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo,
		      uint32_t *curr_limit, uint32_t *supply_voltage)
{
	int i;
	int ma;
	int set_mv = select_mv;
	int max;
	uint32_t flags;

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

	/* build rdo for desired power */
	ma = 10 * (src_caps[i] & 0x3FF);
	max = MIN(ma, MAX_CURRENT_MA);
	flags = (max * set_mv) < (1000 * OPERATING_POWER_MW) ?
			RDO_CAP_MISMATCH : 0;
	*rdo = RDO_FIXED(i + 1, max, max, 0);
	CPRINTF("Request [%d] %dV %dmA", i, set_mv/1000, max);
	/* Mismatch bit set if less power offered than the operating power */
	if (flags & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	*curr_limit = max;
	*supply_voltage = set_mv;
	return EC_SUCCESS;
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
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
	int op_ma = rdo & 0x3FF;
	int max_ma = (rdo >> 10) & 0x3FF;
	int idx = rdo >> 28;
	uint32_t pdo;
	uint32_t pdo_ma;

	if (!idx || idx > pd_src_pdo_cnts[pd_src_pdo_idx])
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

	requested_voltage_idx = idx;

	return EC_SUCCESS;
}

int pd_set_power_supply_ready(int port)
{
	/* Output the correct voltage */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 1);
	gpio_set_level(GPIO_USBC_VSEL_0, requested_voltage_idx >= 2);
	gpio_set_level(GPIO_USBC_VSEL_1, requested_voltage_idx >= 3);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	requested_voltage_idx = 0;
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
	gpio_set_level(GPIO_USBC_VSEL_0, 0);
	gpio_set_level(GPIO_USBC_VSEL_1, 0);
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
