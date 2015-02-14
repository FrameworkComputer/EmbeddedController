/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for PD MCU */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "lightbar.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_PD_HOST_CMD, format, ## args)

#define TASK_EVENT_EXCHANGE_PD_STATUS  TASK_EVENT_CUSTOM(1)

/* By default allow 5V charging only for the dead battery case */
static enum pd_charge_state charge_state = PD_CHARGE_5V;

#define CHARGE_PORT_UNINITIALIZED -2
static int charge_port = CHARGE_PORT_UNINITIALIZED;

void host_command_pd_send_status(enum pd_charge_state new_chg_state)
{
	/* Update PD MCU charge state if necessary */
	if (new_chg_state != PD_CHARGE_NO_CHANGE)
		charge_state = new_chg_state;
	/* Wake PD HC task to send status */
	task_set_event(TASK_ID_PDCMD, TASK_EVENT_EXCHANGE_PD_STATUS, 0);
}

int pd_get_active_charge_port(void)
{
	return charge_port;
}

static void pd_exchange_status(void)
{
	struct ec_params_pd_status ec_status;
	struct ec_response_pd_status pd_status;
	int rv = 0;
#ifdef CONFIG_HOSTCMD_PD_PANIC
	static int pd_in_rw;
#endif

	/* Send PD charge state and battery state of charge */
	ec_status.charge_state = charge_state;
	if (charge_get_flags() & CHARGE_FLAG_BATT_RESPONSIVE)
		ec_status.batt_soc = charge_get_percent();
	else
		ec_status.batt_soc = -1;

	rv = pd_host_command(EC_CMD_PD_EXCHANGE_STATUS, 1, &ec_status,
			     sizeof(struct ec_params_pd_status), &pd_status,
			     sizeof(struct ec_response_pd_status));

	/* If PD doesn't support new command version, try old version */
	if (rv == -EC_RES_INVALID_VERSION)
		rv = pd_host_command(EC_CMD_PD_EXCHANGE_STATUS, 0, &ec_status,
			     sizeof(struct ec_params_pd_status), &pd_status,
			     sizeof(struct ec_response_pd_status));

	if (rv < 0) {
		CPRINTS("Host command to PD MCU failed");
		return;
	}

#ifdef CONFIG_HOSTCMD_PD_PANIC
	/*
	 * Check if PD MCU is in RW. If PD MCU was in RW and is now in RO
	 * AND it did not sysjump to RO, then it must have crashed, and
	 * therefore we should panic as well.
	 */
	if (pd_status.status & PD_STATUS_IN_RW) {
		pd_in_rw = 1;
	} else if (pd_in_rw &&
		   !(pd_status.status & PD_STATUS_JUMPED_TO_IMAGE)) {
		panic_printf("PD crash");
		software_panic(PANIC_SW_PD_CRASH, 0);
	}
#endif

#ifdef HAS_TASK_LIGHTBAR
	/*
	 * If charge port has changed, and it was initialized, then show
	 * battery status on lightbar.
	 */
	if (pd_status.active_charge_port != charge_port) {
		if (charge_port != CHARGE_PORT_UNINITIALIZED) {
			charge_port = pd_status.active_charge_port;
			lightbar_sequence(LIGHTBAR_TAP);
		} else {
			charge_port = pd_status.active_charge_port;
		}
	}
#else
	/* Store the active charge port */
	charge_port = pd_status.active_charge_port;
#endif

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
	pd_exchange_status();

	while (1) {
		/* Wait for the next command event */
		int evt = task_wait_event(-1);

		/* Process event to send status to PD */
		if (evt & TASK_EVENT_EXCHANGE_PD_STATUS)
			pd_exchange_status();
	}
}
