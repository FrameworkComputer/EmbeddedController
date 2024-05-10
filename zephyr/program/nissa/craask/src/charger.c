/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

#include <cros_board_info.h>

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
 * Craask does not have a GPIO indicating whether extpower is present,
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
	if (board_get_usb_pd_port_count() == 2)
		raa489000_hibernate(CHARGER_SECONDARY, true);
	raa489000_hibernate(CHARGER_PRIMARY, true);
	LOG_INF("Charger(s) hibernated");
	cflush();
}

__override int board_get_leave_safe_mode_delay_ms(void)
{
	const struct batt_conf_embed *const batt = get_batt_conf();

	/* If it's COSMX battery, there's need more delay time. */
	if (!strcasecmp(batt->manuf_name, "COSMX KT0030B002") ||
	    !strcasecmp(batt->manuf_name, "COSMX KT0030B004"))
		return 2000;
	else
		return 500;
}

void update_charger_config(void)
{
	uint32_t board_version;
	int ret;

	ret = cbi_get_board_version(&board_version);
	if (ret != 0)
		return;

	/* skip craaskana and craaswell */
	if (board_version == 0x0b || board_version == 0x0D)
		return;

	charger_set_frequency(1050);
}
DECLARE_HOOK(HOOK_INIT, update_charger_config, HOOK_PRIO_DEFAULT);
