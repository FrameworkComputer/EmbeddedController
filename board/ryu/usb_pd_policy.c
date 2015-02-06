/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   900, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(5000, 20000, 10000),
		PDO_VAR(5000, 20000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	struct charge_port_info charge;
	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, port, &charge);
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
				   uint32_t supply_voltage)
{
	struct charge_port_info charge;
	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_TYPEC, port, &charge);
}

int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	return 1;
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
	/* No-operation: we are always 5V */
}

int pd_set_power_supply_ready(int port)
{
	/* provide VBUS */
	gpio_set_level(GPIO_USBC_5V_EN, 1);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	gpio_set_level(GPIO_USBC_5V_EN, 0);
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* TODO: use battery level to decide to accept/reject power swap */
	/* Always allow power swap */
	return 1;
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
	/* TODO: what do we need to do to change host controller data role? */
}

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	return 0;
}
