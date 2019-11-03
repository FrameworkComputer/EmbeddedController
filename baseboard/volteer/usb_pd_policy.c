/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Shared USB-C policy for Volteer boards */
#include "charge_manager.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "usb_pd.h"
#include "system.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/*
 * TODO(b/140578872): Whole file copied from zork. Figure out correct values for
 * volteer.
 */
#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_COMM_CAP)
const uint32_t pd_src_pdo[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};
const uint32_t pd_src_pdo_max[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_max_cnt = ARRAY_SIZE(pd_src_pdo_max);

const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
const uint32_t pd_snk_pdo[] = {
	PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
	PDO_BATT(4750, 21000, 15000),
	PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);
int pd_check_data_swap(int port, int data_role)
{
	/* Allow data swap if we are a UFP, otherwise don't allow. */
	return (data_role == PD_ROLE_UFP);
}

int pd_check_power_swap(int port)
{
	/* TODO - check correct operation for Volteer */

	/*
	 * Allow power swap if we are acting as a dual role device.  If we are
	 * not acting as dual role (ex. suspended), then only allow power swap
	 * if we are sourcing when we could be sinking.
	 */
	if (pd_get_dual_role(port) == PD_DRP_TOGGLE_ON)
		return 1;
	else if (pd_get_role(port) == PD_ROLE_SOURCE)
		return 1;
	else
		return 0;
}

int pd_check_vconn_swap(int port)
{
	/* TODO - check correct operation for Volteer */
	return 0;
}

void pd_power_supply_reset(int port)
{
	/* TODO - check correct operation for Volteer */
}

void pd_execute_data_swap(int port, int data_role)
{
	/* TODO - check correct operation for Volteer */

	/* Do nothing */
}

int pd_is_valid_input_voltage(int mv)
{
	/* TODO - check correct operation for Volteer */

	return 1;
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	/* TODO - check correct operation for Volteer */

	/* No-operation: we are always 5V */
}

int pd_snk_is_vbus_provided(int port)
{
	return ppc_is_vbus_present(port);
}

void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	ppc_set_vbus_source_current_limit(port, rp);
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}


/* ----------------- Vendor Defined Messages ------------------ */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	/* TODO - check correct operation for Volteer */

	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	int is_rw, is_latest;

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
		/* copy hash */
		if (cnt == 7) {
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			is_rw = VDO_INFO_IS_RW(payload[6]);

			is_latest = pd_dev_store_rw_hash(port,
							 dev_id,
							 payload + 1,
							 is_rw ?
							 SYSTEM_IMAGE_RW :
							 SYSTEM_IMAGE_RO);
			/*
			 * Send update host event unless our RW hash is
			 * already known to be the latest update RW.
			 */
			if (!is_rw || !is_latest)
				pd_send_host_event(PD_EVENT_UPDATE_DEVICE);

			CPRINTF("DevId:%d.%d SW:%d RW:%d\n",
				HW_DEV_ID_MAJ(dev_id),
				HW_DEV_ID_MIN(dev_id),
				VDO_INFO_SW_DBG_VER(payload[6]),
				is_rw);
		} else if (cnt == 6) {
			/* really old devices don't have last byte */
			pd_dev_store_rw_hash(port, dev_id, payload + 1,
					     SYSTEM_IMAGE_UNKNOWN);
		}
		break;
	case VDO_CMD_CURRENT:
		CPRINTF("Current: %dmA\n", payload[1]);
		break;
	case VDO_CMD_FLIP:
		usb_mux_flip(port);
		break;
#ifdef CONFIG_USB_PD_LOGGING
	case VDO_CMD_GET_LOG:
		pd_log_recv_vdm(port, cnt, payload);
		break;
#endif /* CONFIG_USB_PD_LOGGING */
	}

	return 0;
}


