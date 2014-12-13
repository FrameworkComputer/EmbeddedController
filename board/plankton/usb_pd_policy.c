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

#define PDO_FIXED_FLAGS (PDO_FIXED_DATA_SWAP | PDO_FIXED_EXTERNAL)

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

void board_set_source_cap(enum board_src_cap cap)
{
	pd_src_pdo_idx = cap;
}

int pd_get_source_pdo(const uint32_t **src_pdo)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnts[pd_src_pdo_idx];
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* No battery, nothing to do */
	return;
}

int pd_check_requested_voltage(uint32_t rdo)
{
	int max_ma = rdo & 0x3FF;
	int op_ma = (rdo >> 10) & 0x3FF;
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

	CPRINTF("Requested %d V %d mA (for %d/%d mA)\n",
		((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	gpio_set_level(GPIO_USBC_VSEL_0, idx >= 2);
	gpio_set_level(GPIO_USBC_VSEL_1, idx >= 3);
}

int pd_set_power_supply_ready(int port)
{
	/* Output the correct voltage */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 1);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
	gpio_set_level(GPIO_USBC_VSEL_0, 0);
	gpio_set_level(GPIO_USBC_VSEL_1, 0);
}

int pd_board_checks(void)
{
	static int was_connected = -1;
	if (was_connected != 1 && pd_is_connected(0))
		board_maybe_reset_usb_hub();
	was_connected = pd_is_connected(0);
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* Always allow power swap */
	return 1;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

void pd_new_contract(int port, int pr_role, int dr_role,
		     int partner_pr_swap, int partner_dr_swap)
{
}

int pd_custom_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	return 0;
}
