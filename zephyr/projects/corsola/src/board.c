/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hooks.h"
#include "typec_control.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include "baseboard_usbc_config.h"

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
	CPRINTS("CCD Enabled, mux DP_AUX_PATH_SEL to 1");
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel), 1);
}
DECLARE_DEFERRED(ccd_interrupt_deferred);

void ccd_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ccd_interrupt_deferred_data, 0);
}
