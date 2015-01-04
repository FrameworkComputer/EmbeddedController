/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "pi3usb9281.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

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
	gpio_set_level(port ? GPIO_USB_C1_5V_EN : GPIO_USB_C0_5V_EN, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	gpio_set_level(port ? GPIO_USB_C1_5V_EN : GPIO_USB_C0_5V_EN, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	struct charge_port_info charge;
	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update(CHARGE_SUPPLIER_PD, port, &charge);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
				   uint32_t supply_voltage)
{
	struct charge_port_info charge;
	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update(CHARGE_SUPPLIER_TYPEC, port, &charge);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
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
	/* Allow data swap if we are a UFP, otherwise don't allow */
	return (data_role == PD_ROLE_UFP) ? 1 : 0;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Open USB switches when taking UFP role */
	pi3usb9281_set_switches(port, (data_role == PD_ROLE_UFP));
}

void pd_check_pr_role(int port, int pr_role, int partner_pr_swap)
{
	/* If sink, and dual role toggling is on, then switch to source */
	if (partner_pr_swap && pr_role == PD_ROLE_SINK &&
	    pd_get_dual_role() == PD_DRP_TOGGLE_ON)
		pd_request_power_swap(port);
}

void pd_check_dr_role(int port, int dr_role, int partner_dr_swap)
{
	/* If UFP, try to switch to DFP */
	if (partner_dr_swap && dr_role == PD_ROLE_UFP)
		pd_request_data_swap(port);
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
	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	int is_rw;
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
		/* copy hash */
		if (cnt == 7) {
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			is_rw = VDO_INFO_IS_RW(payload[6]);
			pd_dev_store_rw_hash(port, dev_id, payload + 1,
					     is_rw ? SYSTEM_IMAGE_RW :
						     SYSTEM_IMAGE_RO);

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
		board_flip_usb_mux(port);
		break;
	}

	return 0;
}

static void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	board_set_usb_mux(port, TYPEC_MUX_NONE, pd_get_polarity(port));
}

static int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);
		return 0;
	}

	return -1;
}

static int dp_on;

static int svdm_dp_status(int port, uint32_t *payload)
{
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_STATUS | VDO_OPOS(pd_alt_mode(port)));
	payload[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				   0, /* HPD level ... not applicable */
				   0, /* exit DP? ... no */
				   0, /* usb mode? ... no */
				   0, /* multi-function ... no */
				   dp_on,
				   0, /* power low? ... no */
				   dp_on);
	return 2;
};

static int svdm_dp_config(int port, uint32_t *payload)
{
	board_set_usb_mux(port, TYPEC_MUX_DP, pd_get_polarity(port));
	dp_on = 1;
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(pd_alt_mode(port)));
	payload[1] = VDO_DP_CFG(MODE_DP_PIN_E, /* sink pins */
				MODE_DP_PIN_E, /* src pins */
				1,             /* DPv1.3 signaling */
				2);            /* UFP connected */
	return 2;
};

static void hpd0_irq_deferred(void)
{
	gpio_set_level(GPIO_USB_C0_DP_HPD, 1);
}

static void hpd1_irq_deferred(void)
{
	gpio_set_level(GPIO_USB_C1_DP_HPD, 1);
}

DECLARE_DEFERRED(hpd0_irq_deferred);
DECLARE_DEFERRED(hpd1_irq_deferred);

#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)

static int svdm_dp_attention(int port, uint32_t *payload)
{
	int cur_lvl;
	int lvl = PD_VDO_HPD_LVL(payload[1]);
	int irq = PD_VDO_HPD_IRQ(payload[1]);
	enum gpio_signal hpd = PORT_TO_HPD(port);
	cur_lvl = gpio_get_level(hpd);
	if (irq & cur_lvl) {
		gpio_set_level(hpd, 0);
		/* 250 usecs is minimum, 2msec is max */
		if (port)
			hook_call_deferred(hpd1_irq_deferred, 300);
		else
			hook_call_deferred(hpd0_irq_deferred, 300);
	} else if (irq & !cur_lvl) {
		CPRINTF("PE ERR: IRQ_HPD w/ HPD_LOW\n");
		return 0; /* nak */
	} else {
		gpio_set_level(hpd, lvl);
	}
	/* ack */
	return 1;
}

static void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
	gpio_set_level(PORT_TO_HPD(port), 0);
}

static int svdm_enter_gfu_mode(int port, uint32_t mode_caps)
{
	/* Always enter GFU mode */
	return 0;
}

static void svdm_exit_gfu_mode(int port)
{
}

static int svdm_gfu_status(int port, uint32_t *payload)
{
	/*
	 * This is called after enter mode is successful, send unstructured
	 * VDM to read info.
	 */
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_READ_INFO, NULL, 0);
	return 0;
}

static int svdm_gfu_config(int port, uint32_t *payload)
{
	return 0;
}

static int svdm_gfu_attention(int port, uint32_t *payload)
{
	return 0;
}

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},
	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	}
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
