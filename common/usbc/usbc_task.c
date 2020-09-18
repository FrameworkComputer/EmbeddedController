/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "board.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_prl_sm.h"
#include "tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"
#include "version.h"

#define USBC_EVENT_TIMEOUT (5 * MSEC)

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint8_t paused[CONFIG_USB_PD_PORT_MAX_COUNT];

void tc_pause_event_loop(int port)
{
	paused[port] = 1;
}

void tc_start_event_loop(int port)
{
	/*
	 * Only generate TASK_EVENT_WAKE event if state
	 * machine is transitioning to un-paused
	 */
	if (paused[port]) {
		paused[port] = 0;
		task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE, 0);
	}
}

static void pd_task_init(int port)
{
	if (IS_ENABLED(CONFIG_USB_TYPEC_SM))
		tc_state_init(port);
	paused[port] = 0;

	/*
	 * Since most boards configure the TCPC interrupt as edge
	 * and it is possible that the interrupt line was asserted between init
	 * and calling set_state, we need to process any pending interrupts now.
	 * Otherwise future interrupts will never fire because another edge
	 * never happens. Note this needs to happen after set_state() is called.
	 */
	if (IS_ENABLED(CONFIG_HAS_TASK_PD_INT))
		schedule_deferred_pd_interrupt(port);
}

static bool pd_task_loop(int port)
{
	/* wait for next event/packet or timeout expiration */
	const uint32_t evt =
		task_wait_event(paused[port]
					? -1
					: USBC_EVENT_TIMEOUT);

	/*
	 * Re-use TASK_EVENT_RESET_DONE in tests to restart the USB task
	 * if this code is running in a unit test.
	 */
	if (IS_ENABLED(TEST_BUILD) && (evt & TASK_EVENT_RESET_DONE))
		return false;

	/* handle events that affect the state machine as a whole */
	if (IS_ENABLED(CONFIG_USB_TYPEC_SM))
		tc_event_check(port, evt);

	/*
	 * run port controller task to check CC and/or read incoming
	 * messages
	 */
	if (IS_ENABLED(CONFIG_USB_PD_TCPC))
		tcpc_run(port, evt);

	/* Run policy engine state machine */
	if (IS_ENABLED(CONFIG_USB_PE_SM))
		pe_run(port, evt, tc_get_pd_enabled(port));

	/* Run protocol state machine */
	if (IS_ENABLED(CONFIG_USB_PRL_SM) || IS_ENABLED(CONFIG_TEST_USB_PE_SM))
		prl_run(port, evt, tc_get_pd_enabled(port));

	/* Run TypeC state machine */
	if (IS_ENABLED(CONFIG_USB_TYPEC_SM))
		tc_run(port);

	return true;
}

void pd_task(void *u)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * If port does not exist, return
	 */
	if (port >= board_get_usb_pd_port_count())
		return;

	while (1) {
		pd_task_init(port);

		/* As long as pd_task_loop returns true, keep running the loop.
		 * pd_task_loop returns false when the code needs to re-init
		 * the task, so once the code breaks out of the inner while
		 * loop, the re-init code at the top of the outer while loop
		 * will run.
		 */
		while (pd_task_loop(port))
			continue;
	}
}
