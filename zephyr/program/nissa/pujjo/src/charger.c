/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "console.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/raa489000.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charger, LOG_LEVEL_INF);

int extpower_is_present(void)
{
	int port;
	int rv;
	bool acok;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		rv = raa489000_is_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	return 0;
}

/*
 * Pujjo does not have a GPIO indicating whether extpower is present,
 * so detect using the charger(s).
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;
}

__override void board_hibernate(void)
{
	/* Shut down the chargers */
	raa489000_hibernate(0, true);
	LOG_INF("Charger(s) hibernated");
	cflush();
}

static void charger_prochot_init(void)
{
	isl923x_set_ac_prochot(CHARGER_SOLO, 3500);
	isl923x_set_dc_prochot(CHARGER_SOLO, 6528);
}
DECLARE_HOOK(HOOK_INIT, charger_prochot_init, HOOK_PRIO_POST_FIRST);
