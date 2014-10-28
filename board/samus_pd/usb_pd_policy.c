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
#include "registers.h"
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

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   900, PDO_FIXED_DUAL_ROLE),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_DUAL_ROLE),
		PDO_BATT(5000, 20000, 15000),
		PDO_VAR(5000, 20000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Cap on the max voltage requested as a sink (in millivolts) */
static unsigned max_mv = -1; /* no cap */

int pd_choose_voltage_common(int cnt, uint32_t *src_caps, uint32_t *rdo,
			     uint32_t *curr_limit, uint32_t *supply_voltage,
			     int choose_min)
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
		/*
		 * Choose the first entry if seaching for minimum, which will
		 * always be vSafe5V.
		 */
		if (choose_min)
			break;
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
		/*
		 * If we're choosing the minimum charge mode, limit our current
		 * to what we can set with ilim PWM (500mA)
		 */
		max = MIN(ma, choose_min ? CONFIG_CHARGER_INPUT_CURRENT :
					   MAX_CURRENT_MA);
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

int pd_choose_voltage_min(int cnt, uint32_t *src_caps, uint32_t *rdo,
			  uint32_t *curr_limit, uint32_t *supply_voltage)
{
	return pd_choose_voltage_common(cnt, src_caps, rdo, curr_limit,
					supply_voltage, 1);
}

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo,
		      uint32_t *curr_limit, uint32_t *supply_voltage)
{
	return pd_choose_voltage_common(cnt, src_caps, rdo, curr_limit,
					supply_voltage, 0);
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

int pd_power_swap(int port)
{
	/* TODO: use battery level to decide to accept/reject power swap */
	/* Always allow power swap */
	return 1;
}
/* ----------------- Vendor Defined Messages ------------------ */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

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
			/* send host event */
			pd_send_host_event(PD_EVENT_UPDATE_DEVICE);

			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			CPRINTF("Dev:0x%04x SW:%d RW:%d\n", dev_id,
				VDO_INFO_SW_DBG_VER(payload[6]),
				VDO_INFO_IS_RW(payload[6]));
		}
		/* copy hash */
		if (cnt >= 6)
			pd_dev_store_rw_hash(port, dev_id, payload + 1);

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

int pd_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	if (PD_VDO_SVDM(payload[0]))
		return pd_svdm(port, cnt, payload, rpayload);
	else
		return pd_custom_vdm(port, cnt, payload, rpayload);
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

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
