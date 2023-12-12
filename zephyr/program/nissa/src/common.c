/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "hooks.h"
#include "nissa_common.h"
#include "system.h"
#include "usb_mux.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>
LOG_MODULE_REGISTER(nissa, CONFIG_NISSA_LOG_LEVEL);

__overridable void board_power_change(struct ap_power_ev_callback *cb,
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

static void board_setup_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, board_power_change,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);
}
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_INIT_I2C);

int pd_check_vconn_swap(int port)
{
	/* Do not allow vconn swap if 5V rail is off. */
	const struct gpio_dt_spec *const ec_soc_dsw_pwrok_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_dsw_pwrok);

	return gpio_pin_get_dt(ec_soc_dsw_pwrok_gpio);
}

/*
 * Count of chargers depends on sub board presence.
 */
__override uint8_t board_get_charger_chip_count(void)
{
#ifdef CONFIG_PLATFORM_EC_CHARGER_SINGLE_CHIP
	return CHARGER_NUM;
#else
	return board_get_usb_pd_port_count();
#endif /* CONFIG_PLATFORM_EC_CHARGER_SINGLE_CHIP */
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

static int battery_cells;

test_export_static void board_get_battery_cells(void)
{
	if (charger_get_battery_cells(CHARGER_PRIMARY, &battery_cells) ==
	    EC_SUCCESS) {
		LOG_INF("battery_cells:%d", battery_cells);
	} else {
		LOG_ERR("Failed to get default battery type");
	}
}
DECLARE_HOOK(HOOK_INIT, board_get_battery_cells, HOOK_PRIO_DEFAULT);

/*
 * Called by USB-PD code to determine whether a given input voltage is
 * acceptable.
 */
__override int pd_is_valid_input_voltage(int mv)
{
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
	 * We assume that any battery with a design 3S, and
	 * that other problematic PD voltages (near to, but not exactly 12V)
	 * will rarely occur.
	 */
	if (battery_cells == 3 && mv == 12000) {
		return false;
	}
	return true;
}
#endif

#ifdef CONFIG_SOC_IT8XXX2
static void it8xxx2_i2c_swap_default(void)
{
	/* Channel A and B are located at SMCLK0/SMDAT0 and SMCLK1/SMDAT1. */
	IT8XXX2_SMB_SMB01CHS = 0x10;
	/* Channel C and D are located at SMCLK2/SMDAT2 and SMCLK3/SMDAT3. */
	IT8XXX2_SMB_SMB23CHS = 0x32;
}
DECLARE_HOOK(HOOK_SYSJUMP, it8xxx2_i2c_swap_default, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_SOC_IT8XXX2 */

/* Trigger shutdown by enabling the Z-sleep circuit */
__override void board_hibernate_late(void)
{
#ifndef CONFIG_PLATFORM_EC_HIBERNATE_PSL
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
#endif
}

#ifdef CONFIG_OCPC
__override void board_ocpc_init(struct ocpc_data *ocpc)
{
	/* Ensure board has at least 2 charger chips. */
	if (board_get_charger_chip_count() > 1) {
		/* There's no provision to measure Isys */
		ocpc->chg_flags[CHARGER_SECONDARY] |= OCPC_NO_ISYS_MEAS_CAP;
	}
}
#endif

int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	/*
	 * AP tunneling to I2C is default-forbidden, but allowed for
	 * type-C ports because these can be used to update TCPC or retimer
	 * firmware. AP firmware separately sends a command to block tunneling
	 * to these ports after it's done updating chips.
	 */
	return false
#if DT_NODE_EXISTS(DT_NODELABEL(tcpc_port0))
	       || (cmd_desc->port == I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_port0)))
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(tcpc_port1))
	       || (cmd_desc->port == I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_port1)))
#endif
		;
}
