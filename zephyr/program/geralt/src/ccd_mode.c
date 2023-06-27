/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "system.h"
#include "typec_control.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ##args)

static void ccd_interrupt_deferred(void)
{
	/*
	 * If CCD_MODE_ODL asserts, it means there's a debug accessory connected
	 * and we should enable the SBU FETs.
	 */
	typec_set_sbu(CONFIG_CCD_USBC_PORT_NUMBER, 1);

	/* Mux DP AUX away when CCD enabled to prevent the AUX channel
	 * interferes the SBU pins.
	 */
	CPRINTS("CCD Enabled, mux DP_PATH_SEL to 1");
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_path_sel), 1);
}
DECLARE_DEFERRED(ccd_interrupt_deferred);

void ccd_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ccd_interrupt_deferred_data, 0);
}

static void ccd_mode_init(void)
{
	/* If CCD mode has enabled before init, force the ccd_interrupt. */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ccd_mode_odl))) {
		ccd_interrupt(GPIO_CCD_MODE_ODL);
	}
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ccd_mode_odl));
}
DECLARE_HOOK(HOOK_INIT, ccd_mode_init, HOOK_PRIO_PRE_DEFAULT);

__override void board_pulse_entering_rw(void)
{
	/* no-op for dauntless */
}
