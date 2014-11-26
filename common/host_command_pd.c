/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for PD MCU */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_PD_HOST_CMD, format, ## args)

#define TASK_EVENT_EXCHANGE_PD_STATUS  TASK_EVENT_CUSTOM(1)

void host_command_pd_send_status(void)
{
	task_set_event(TASK_ID_PDCMD, TASK_EVENT_EXCHANGE_PD_STATUS, 0);
}

void pd_exchange_status(int *charge_port)
{
	struct ec_params_pd_status ec_status;
	struct ec_response_pd_status pd_status = {
		/* default for when the PD isn't cooperating */
		.active_charge_port = -1,
	};
	int rv = 0;

	/* Send battery state of charge */
	if (charge_get_flags() & CHARGE_FLAG_BATT_RESPONSIVE)
		ec_status.batt_soc = charge_get_percent();
	else
		ec_status.batt_soc = -1;

	rv = pd_host_command(EC_CMD_PD_EXCHANGE_STATUS, 0, &ec_status,
			     sizeof(struct ec_params_pd_status), &pd_status,
			     sizeof(struct ec_response_pd_status));

	if (charge_port)
		*charge_port = pd_status.active_charge_port;

	if (rv < 0) {
		CPRINTS("Host command to PD MCU failed");
		return;
	}

	/* Set input current limit */
	rv = charge_set_input_current_limit(MAX(pd_status.curr_lim_ma,
					CONFIG_CHARGER_INPUT_CURRENT));
	if (rv < 0)
		CPRINTS("Failed to set input current limit from PD MCU");

	/* If PD is signalling host event, then pass it up to AP */
	if (pd_status.status & PD_STATUS_HOST_EVENT)
		host_set_single_event(EC_HOST_EVENT_PD_MCU);
}

void pd_command_task(void)
{
	/* On startup exchange status with the PD */
	pd_exchange_status(0);

	while (1) {
		/* Wait for the next command event */
		int evt = task_wait_event(-1);

		/* Process event to send status to PD */
		if (evt & TASK_EVENT_EXCHANGE_PD_STATUS)
			pd_exchange_status(0);
	}
}
