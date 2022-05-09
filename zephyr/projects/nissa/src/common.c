/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <ap_power/ap_power.h>

#include "battery.h"
#include "charger.h"
#include "charge_state_v2.h"
#include "chipset.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_mux.h"
#include "system.h"
#include "throttle_ap.h"

#include "nissa_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nissa, CONFIG_NISSA_LOG_LEVEL);

#define PROCHOT_GPIO_ENUM \
	GPIO_SIGNAL(DT_PROP(DT_NODELABEL(int_prochot), irq_pin))

static const struct prochot_cfg nissa_prochot_cfg = {
	.gpio_prochot_in = PROCHOT_GPIO_ENUM,
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
	{ /* sub-board */
		.usb_port = 1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		/*
		 * next_mux filled in by board config code
		 * if sub-board has type-C USB port.
		 */
	},
};

static uint8_t cached_usb_pd_port_count;

__override uint8_t board_get_usb_pd_port_count(void)
{
	__ASSERT(cached_usb_pd_port_count != 0,
		 "sub-board detection did not run before a port count request");
	if (cached_usb_pd_port_count == 0)
		LOG_WRN("USB PD Port count not initialized!");
	return cached_usb_pd_port_count;
}

static void board_power_change(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;
	}
}

/*
 * Initialise the USB PD port count, which
 * depends on which sub-board is attached.
 */
static void board_setup_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, board_power_change,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	switch (nissa_get_sb_type()) {
	default:
		cached_usb_pd_port_count = 1;
		break;

	case NISSA_SB_C_A:
	case NISSA_SB_C_LTE:
		cached_usb_pd_port_count = 2;
		break;
	}

	/* Enable PROCHOT monitoring */
	throttle_ap_config_prochot(&nissa_prochot_cfg);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_prochot));
}
/*
 * Make sure setup is done after EEPROM is readable.
 */
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_INIT_I2C);

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int icl = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);

	/*
	 * Assume charger overdraws by about 4%, keeping the actual draw
	 * within spec. This adjustment can be changed with characterization
	 * of actual hardware.
	 */
	icl = icl * 96 / 100;
	charge_set_input_current_limit(icl, charge_mv);
}

int pd_check_vconn_swap(int port)
{
	/* Allow VCONN swaps if the AP is on. */
	return chipset_in_state(CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON);
}

/*
 * Count of chargers depends on sub board presence.
 */
__override uint8_t board_get_charger_chip_count(void)
{
	return board_get_usb_pd_port_count();
}

/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum nissa_sub_board_type nissa_get_sb_type(void)
{
	static enum nissa_sub_board_type sb = NISSA_SB_UNKNOWN;
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (sb != NISSA_SB_UNKNOWN)
		return sb;

	sb = NISSA_SB_NONE;	/* Defaults to none */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return sb;
	}
	switch (val) {
	default:
		LOG_WRN("No sub-board defined");
		break;
	case FW_SUB_BOARD_1:
		sb = NISSA_SB_C_A;
		LOG_INF("SB: USB type C, USB type A");
		break;

	case FW_SUB_BOARD_2:
		sb = NISSA_SB_C_LTE;
		LOG_INF("SB: USB type C, WWAN LTE");
		break;

	case FW_SUB_BOARD_3:
		sb = NISSA_SB_HDMI_A;
		LOG_INF("SB: HDMI, USB type A");
		break;
	}
	return sb;
}
