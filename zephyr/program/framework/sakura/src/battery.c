/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>


#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"
#include "board_function.h"
#include "board_host_command.h"
#include "charger.h"
#include "charge_state.h"
#include "charge_manager.h"
#include "cypress_pd_common.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "extpower.h"
#include "hooks.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

#define CHARGER_RELEASE_BPLUS_VOLATGE_TIMER (5 * SECOND)
/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA 0x0010

void board_cut_off(void)
{
	int rv;

	/* Ship mode command requires writing 2 data values */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	rv |= sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);

	if (rv == EC_RES_SUCCESS) {
		CPRINTS("Battery cutoff is successful");
		set_battery_in_cut_off();
	} else {
		CPRINTS("Battery cutoff has failed");
	}
}
DECLARE_DEFERRED(board_cut_off);

__override int board_cut_off_battery(void)
{
	int timer = 0;
	bool ec_control = true;
	bool pd_port_on = false;

	if (extpower_is_present()) {

		/**
		 * Disable all PD ports to prevent non-active ports from
		 * sourcing power post-cutoff.
		 */
		for (int port = 0; port < PD_PORT_COUNT; port++)
			cypd_cfet_vbus_control(port, pd_port_on, ec_control);
		timer = CHARGER_RELEASE_BPLUS_VOLATGE_TIMER;
	}

	hook_call_deferred(&board_cut_off_data, timer);

	return EC_RES_ERROR;
}

static enum ec_status cmd_get_cutoff_status(struct host_cmd_handler_args *args)
{
	struct ec_response_get_cutoff_status *r = args->response;

	r->status = battery_is_cut_off();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CUTOFF_STATUS, cmd_get_cutoff_status,
			EC_VER_MASK(0));
