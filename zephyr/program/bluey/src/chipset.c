/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bluey chipset-specific configuration */

#include "adsp_comms.h"
#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power/qcom.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

static void adsp_power_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	CPRINTS("ADSP Power State: 0x%04x", data);
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_VAL,
		       adsp_power_state_cb);

static void enable_acok_passthru_interrupt(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_acok_passthru));

	/* Initial update of the passthru signal */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
		passthru_ac_on_to_pmic();
}
DECLARE_HOOK(HOOK_INIT, enable_acok_passthru_interrupt, HOOK_PRIO_DEFAULT);

void passthru_lid_open_to_pmic(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od),
			gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_lid_open)));
}

void passthru_ac_on_to_pmic(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok),
			extpower_is_present());
}

void chipset_acok_passthru_interrupt(enum gpio_signal signal)
{
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
		passthru_ac_on_to_pmic();
}

void reset_all_passthru_pmic_signal(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od), 0);
}
