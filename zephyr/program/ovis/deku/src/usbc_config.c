/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/bb_retimer_public.h"
#include "gpio.h"
#include "timer.h"
#include "usbc_config.h"

void board_reset_pd_mcu(void)
{
	/* Required for build */
}

__override int bb_retimer_power_enable(const struct usb_mux *me, bool enable)
{
	const struct bb_usb_control *control = &bb_controls[me->usb_port];
	/* handle retimer's power domain */
	if (enable) {
		gpio_set_level(control->usb_ls_en_gpio, 1);
		/*
		 * Tpw, minimum time from VCC to RESET_N de-assertion is 100us.
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 * For deku, we need 8ms delay before RT_RST de-assertion, see
		 * b:346883913.
		 */
		crec_msleep(8);
		gpio_set_level(control->retimer_rst_gpio, 1);
		/*
		 * Allow 1ms time for the retimer to power up lc_domain
		 * which powers I2C controller within retimer
		 */
		crec_msleep(1);
	} else {
		gpio_set_level(control->retimer_rst_gpio, 0);
		crec_msleep(1);
		gpio_set_level(control->usb_ls_en_gpio, 0);
	}
	return EC_SUCCESS;
}
