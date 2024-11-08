/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "cypress_pd_common.h"

static void board_change_cypd_init_state(void)
{
	int controller;

	/*
	 * The PD1 firmware adds the hard code to delay 415ms, in this duration,
	 * the PD chip cannot communicate via i2c.
	 * EC changes the initial state to CCG_STATE_WAIT_STABLE to wait the PD
	 * exits the delay.
	 */
	for (controller = 0; controller < PD_CHIP_COUNT; controller++)
		pd_chip_config[controller].state = CCG_STATE_WAIT_STABLE;
}
DECLARE_HOOK(HOOK_INIT, board_change_cypd_init_state, HOOK_PRIO_DEFAULT);

void cypd_ccd_mode_control(void)
{
	if (pd_port_states[CONFIG_PD_CCG6_EC_UART_DEBUG_PORT].c_state == CCG_STATUS_DEBUG ||
		pd_port_states[CONFIG_PD_CCG6_SOC_UART_DEBUG_PORT].c_state == CCG_STATUS_DEBUG) {
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_mux_uart_flip), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_mux_uart_flip), 0);
	}
}
