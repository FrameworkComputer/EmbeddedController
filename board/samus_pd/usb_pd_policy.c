/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Define typical operating power and max power */
#define OPERATING_POWER_MW 15000
#define MAX_POWER_MW       60000
#define MAX_CURRENT_MA     3000

/*
 * Do not request any voltage within this deadband region, where
 * we're not sure whether or not the boost or the bypass will be on.
 */
#define INPUT_VOLTAGE_DEADBAND_MIN 9700
#define INPUT_VOLTAGE_DEADBAND_MAX 11999

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   900, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(4750, 21000, 15000),
		PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

__override int pd_is_valid_input_voltage(int mv)
{
	/* Allow any voltage not in the boost bypass deadband */
	return  (mv < INPUT_VOLTAGE_DEADBAND_MIN) ||
		(mv > INPUT_VOLTAGE_DEADBAND_MAX);
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

int pd_snk_is_vbus_provided(int port)
{
	return gpio_get_level(port ? GPIO_USB_C1_VBUS_WAKE :
				     GPIO_USB_C0_VBUS_WAKE);
}

int pd_check_vconn_swap(int port)
{
	/* in S5, do not allow vconn swap since pp5000 rail is off */
	return gpio_get_level(GPIO_PCH_SLP_S5_L);
}

/* ----------------- Vendor Defined Messages ------------------ */
#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
__override void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

	gpio_set_level(PORT_TO_HPD(port), 1);
}

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
#define PORT_TO_HPD_IRQ_DEFERRED(port) ((port) ?			\
					&hpd1_irq_deferred_data :	\
					&hpd0_irq_deferred_data)

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	int cur_lvl;
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	enum gpio_signal hpd = PORT_TO_HPD(port);
	cur_lvl = gpio_get_level(hpd);

	dp_status[port] = payload[1];

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

	if (irq & cur_lvl) {
		gpio_set_level(hpd, 0);
		hook_call_deferred(PORT_TO_HPD_IRQ_DEFERRED(port),
				   HPD_DSTREAM_DEBOUNCE_IRQ);
	} else if (irq & !cur_lvl) {
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	} else {
		gpio_set_level(hpd, lvl);
	}
	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
	gpio_set_level(PORT_TO_HPD(port), 0);
}
