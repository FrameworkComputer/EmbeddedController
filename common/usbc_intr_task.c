/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* High-priority interrupt tasks implementations */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "builtin/assert.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <stdint.h>

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* Events for pd_interrupt_handler_task */
#define PD_PROCESS_INTERRUPT BIT(0)

#define ALERT_STORM_INTERVAL SECOND

static uint8_t pd_int_task_id[CONFIG_USB_PD_PORT_MAX_COUNT];

test_mockable void schedule_deferred_pd_interrupt(const int port)
{
	/*
	 * Don't set event to idle task if task id is 0. This happens when
	 * not all the port have pd int task, the pd_int_task_id of port
	 * that doesn't have pd int task is 0.
	 */
	if (pd_int_task_id[port] != 0)
		task_set_event(pd_int_task_id[port], PD_PROCESS_INTERRUPT);
}

static struct {
	int count;
	timestamp_t time;
} storm_tracker[CONFIG_USB_PD_PORT_MAX_COUNT];

static void service_one_port(int port)
{
	timestamp_t now;

	tcpc_alert(port);

	now = get_time();
	if (timestamp_expired(storm_tracker[port].time, &now)) {
		/* Reset timer into future */
		storm_tracker[port].time.val = now.val + ALERT_STORM_INTERVAL;

		/*
		 * Start at 1 since we are processing an interrupt right
		 * now
		 */
		storm_tracker[port].count = 1;
	} else if (++storm_tracker[port].count > CONFIG_USB_PD_INT_STORM_MAX) {
		CPRINTS("C%d: Interrupt storm detected."
			" Disabling port temporarily",
			port);

		pd_set_suspend(port, 1);
		pd_deferred_resume(port);
	}
}

__overridable void board_process_pd_alert(int port)
{
}

/*
 * Main task entry point that handles PD interrupts for a single port.  These
 * interrupts usually come from a TCPC, but may also come from PD-related chips
 * sharing the TCPC interrupt line.
 *
 * @param p The PD port number for which to handle interrupts (pointer is
 * reinterpreted as an integer directly).
 */
void pd_interrupt_handler_task(void *p)
{
	const int port = (int)((intptr_t)p);
	const int port_mask = (PD_STATUS_TCPC_ALERT_0 << port);

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	/*
	 * If port does not exist, return
	 */
	if (port >= board_get_usb_pd_port_count())
		return;

	pd_int_task_id[port] = task_get_current();

	while (1) {
		const int evt = task_wait_event(-1);

		if ((evt & PD_PROCESS_INTERRUPT) == 0)
			continue;
		/*
		 * While the interrupt signal is asserted; we have more
		 * work to do. This effectively makes the interrupt a
		 * level-interrupt instead of an edge-interrupt without
		 * having to enable/disable a real level-interrupt in
		 * multiple locations.
		 *
		 * Also, if the port is disabled do not process
		 * interrupts. Upon existing suspend, we schedule a
		 * PD_PROCESS_INTERRUPT to check if we missed anything.
		 */
		while ((tcpc_get_alert_status() & port_mask) &&
		       pd_is_port_enabled(port)) {
			service_one_port(port);
		}

		board_process_pd_alert(port);
	}
}

/*
 * This code assumes port alert masks are adjacent to each other.
 */
BUILD_ASSERT(PD_STATUS_TCPC_ALERT_3 == (PD_STATUS_TCPC_ALERT_0 << 3));

/*
 * Shared TCPC interrupt handler. The function argument in ec.tasklist
 * is the mask of ports to handle. For example:
 *
 *    BIT(USBC_PORT_C2) | BIT(USBC_PORT_C0)
 *
 * Note that this bitmask is 0-based while PD_STATUS_TCPC_ALERT_<port>
 * is not.
 */

#if !defined(CONFIG_ZEPHYR) || defined(CONFIG_HAS_TASK_PD_INT_SHARED)
void pd_shared_alert_task(void *p)
{
	const int sources_mask = (int)((intptr_t)p);
	int want_alerts = 0;
	int port;
	int port_mask;

	CPRINTS("%s: port mask 0x%02x", __func__, sources_mask);

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port) {
		if ((sources_mask & BIT(port)) == 0)
			continue;
		if (!board_is_usb_pd_port_present(port))
			continue;

		port_mask = PD_STATUS_TCPC_ALERT_0 << port;
		want_alerts |= port_mask;
		pd_int_task_id[port] = task_get_current();
	}

	if (want_alerts == 0) {
		/*
		 * None of the configured alert sources are available.
		 */
		return;
	}

	while (1) {
		const int evt = task_wait_event(-1);
		int have_alerts;

		if ((evt & PD_PROCESS_INTERRUPT) == 0)
			continue;

		/*
		 * While the interrupt signal is asserted; we have more
		 * work to do. This effectively makes the interrupt a
		 * level-interrupt instead of an edge-interrupt without
		 * having to enable/disable a real level-interrupt in
		 * multiple locations.
		 *
		 * Also, if the port is disabled do not process
		 * interrupts. Upon existing suspend, we schedule a
		 * PD_PROCESS_INTERRUPT to check if we missed anything.
		 */
		do {
			have_alerts = tcpc_get_alert_status();
			have_alerts &= want_alerts;

			for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT;
			     ++port) {
				port_mask = PD_STATUS_TCPC_ALERT_0 << port;
				if ((have_alerts & port_mask) == 0) {
					/* skip quiet port */
					continue;
				}
				if (!pd_is_port_enabled(port)) {
					/* filter out disabled port */
					have_alerts &= ~port_mask;
					continue;
				}
				service_one_port(port);
			}
			for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT;
			     ++port) {
				board_process_pd_alert(port);
			}
		} while (have_alerts != 0);
	}
}
#endif /* !CONFIG_ZEPHYR || CONFIG_HAS_TASK_PD_INT_SHARED */
