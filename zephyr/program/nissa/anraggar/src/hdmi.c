/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "nissa_hdmi.h"

#include <cros_board_info.h>

__override void nissa_configure_hdmi_power_gpios(void)
{
	/*
	 * Nereid versions before 2 need hdmi-en-odl to be
	 * pulled down to enable VCC on the HDMI port, but later
	 * versions (and other boards) disconnect this so
	 * the port's VCC directly follows en-rails-odl. Only
	 * configure the GPIO if needed, to save power.
	 */
	uint32_t board_version = 0;

	/* CBI errors ignored, will configure the pin */
	cbi_get_board_version(&board_version);
	if (board_version < 2) {
		nissa_configure_hdmi_vcc();
	}

	/* Still always need core rails controlled */
	nissa_configure_hdmi_rails();
}
