/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
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

#define PDO_FIXED_FLAGS (PDO_FIXED_EXTERNAL | PDO_FIXED_DATA_SWAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(5000, 20000, 15000),
		PDO_VAR(5000, 20000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	int red = supply_voltage == 20000;
	int green = supply_voltage == 5000;
	int blue = supply_voltage && !(red || green);
	gpio_set_level(GPIO_LED_R_L, !red);
	gpio_set_level(GPIO_LED_G_L, !green);
	gpio_set_level(GPIO_LED_B_L, !blue);
}

int pd_check_requested_voltage(uint32_t rdo)
{
	int max_ma = rdo & 0x3FF;
	int op_ma = (rdo >> 10) & 0x3FF;
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

	CPRINTF("Requested %d V %d mA (for %d/%d mA)\n",
		((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* Always refuse power swap */
	return 0;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

void pd_check_pr_role(int port, int pr_role, int partner_pr_swap)
{
}

void pd_check_dr_role(int port, int dr_role, int partner_dr_swap)
{
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	return 0;
}
