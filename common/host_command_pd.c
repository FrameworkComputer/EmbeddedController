/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for PD MCU */

#include "charge_state.h"
#include "common.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define TASK_EVENT_EXCHANGE_PD_STATUS  TASK_EVENT_CUSTOM(1)

void host_command_pd_send_status(void)
{
	task_set_event(TASK_ID_PDCMD, TASK_EVENT_EXCHANGE_PD_STATUS, 0);
}

static void pd_exchange_status(void)
{
	struct ec_params_pd_status ec_status;

	/*
	 * TODO(crosbug.com/p/29499): Change sending state of charge to
	 * remaining capacity for finer grained control.
	 */
	/* Send battery state of charge */
	if (charge_get_flags() & CHARGE_FLAG_BATT_RESPONSIVE)
		ec_status.batt_soc = charge_get_percent();
	else
		ec_status.batt_soc = -1;

	pd_host_command(EC_CMD_PD_EXCHANGE_STATUS, 0, &ec_status,
			sizeof(struct ec_params_pd_status), NULL, 0);
}

void pd_command_task(void)
{

	while (1) {
		/* Wait for the next command event */
		int evt = task_wait_event(-1);

		/* Process event to send status to PD */
		if (evt & TASK_EVENT_EXCHANGE_PD_STATUS)
			pd_exchange_status();
	}
}

