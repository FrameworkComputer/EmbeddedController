/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "usbc_ppc.h"

int board_set_active_charge_port(int port)
{
	return 0;
}

int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
}

const struct batt_params *charger_current_battery_params(void)
{
	static const struct batt_params params = {};

	return &params;
}

int board_get_battery_soc(void)
{
	return 0;
}

void pd_power_supply_reset(void)
{
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
}

int pd_set_power_supply_ready(int port)
{
	return 0;
}

void usb_charger_vbus_change(int port, int vbus_level)
{
}

int charge_manager_get_active_charge_port(void)
{
	return 0;
}

struct ppc_config_t ppc_chips[] = {};

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

#ifdef CONFIG_MUX_INIT_ADC
const struct adc_t adc_channels[] = {};
#endif
