/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "chipset.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "nissa_common.h"
#include "system.h"
#include "usb_mux.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>
LOG_MODULE_REGISTER(nissa, CONFIG_NISSA_LOG_LEVEL);

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
	/*
	 * Enable power to pen garage when system is active (safe even if no
	 * pen is present).
	 */
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);

	switch (data.event) {
	case AP_POWER_STARTUP:
		gpio_pin_set_dt(pen_power_gpio, 1);
		break;
	case AP_POWER_SHUTDOWN:
		gpio_pin_set_dt(pen_power_gpio, 0);
		break;
	default:
		break;
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
}
/*
 * Make sure setup is done after EEPROM is readable.
 */
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_INIT_I2C);

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

	sb = NISSA_SB_NONE; /* Defaults to none */
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

__override void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
				       int *ki_div, int *kd, int *kd_div)
{
	*kp = 1;
	*kp_div = 32;
	*ki = 0;
	*ki_div = 1;
	*kd = 0;
	*kd_div = 1;
}

#ifdef CONFIG_PLATFORM_EC_CHARGER_SM5803
/*
 * Called by USB-PD code to determine whether a given input voltage is
 * acceptable.
 */
__override int pd_is_valid_input_voltage(int mv)
{
	int battery_voltage, rv;

	rv = battery_design_voltage(&battery_voltage);
	if (rv) {
		LOG_ERR("Unable to get battery design voltage: %d", rv);
		return true;
	}

	/*
	 * SM5803 is extremely inefficient in buck-boost mode, when
	 * VBUS ~= VSYS: very high temperatures on the chip and associated
	 * inductor have been observed when sinking normal charge current in
	 * buck-boost mode (but not in buck or boost mode) so we choose to
	 * completely exclude some voltages that are likely to be problematic.
	 *
	 * Nissa devices use either 2S or 3S batteries, for which VBUS will
	 * usually only be near VSYS with a 3S battery and 12V input (picked
	 * from among common supported PD voltages)- 2S can get close to
	 * 9V, but we expect charge current to be low when a 2S battery is
	 * charged to that voltage (because it will be nearly full).
	 *
	 * We assume that any battery with a design voltage above 9V is 3S, and
	 * that other problematic PD voltages (near to, but not exactly 12V)
	 * will rarely occur.
	 */
	if (battery_voltage > 9000 && mv == 12000) {
		return false;
	}
	return true;
}
#endif

/* Trigger shutdown by enabling the Z-sleep circuit */
__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
}
