/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
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
	int rv = charge_set_input_current_limit(MAX(max_ma,
					CONFIG_CHARGER_INPUT_CURRENT));
	if (rv < 0)
		CPRINTS("Failed to set input current limit for PD");
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

void pd_new_contract(int port, int pr_role, int dr_role,
		     int partner_pr_swap, int partner_dr_swap)
{
}

void pd_execute_data_swap(int port, int data_role)
{
	/* TODO: what do we need to do to change host controller data role? */
}

/* ----------------- Vendor Defined Messages ------------------ */
static int pd_custom_vdm(int port, int cnt, uint32_t *payload,
			 uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	CPRINTF("VDM/%d [%d] %08x\n", cnt, cmd, payload[0]);

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		CPRINTF("version: %s\n", (char *)(payload+1));
		break;
	case VDO_CMD_READ_INFO:
	case VDO_CMD_SEND_INFO:
		/* if last word is present, it contains lots of info */
		if (cnt == 7) {
			/* TODO: Notify host */
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			CPRINTF("DevId:%d.%d SW:%d RW:%d\n",
				HW_DEV_ID_MAJ(dev_id),
				HW_DEV_ID_MIN(dev_id),
				VDO_INFO_SW_DBG_VER(payload[6]),
				VDO_INFO_IS_RW(payload[6]));
		}
		/* copy hash */
		if (cnt >= 6)
			pd_dev_store_rw_hash(port, dev_id, payload + 1,
					     SYSTEM_IMAGE_UNKNOWN);

		break;
	case VDO_CMD_CURRENT:
		CPRINTF("Current: %dmA\n", payload[1]);
		break;
	case VDO_CMD_FLIP:
		board_flip_usb_mux(0);
		break;
	}

	return 0;
}

int pd_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	if (PD_VDO_SVDM(payload[0]))
		return pd_svdm(port, cnt, payload, rpayload);
	else
		return pd_custom_vdm(port, cnt, payload, rpayload);
}
