/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* High-priority interrupt tasks implementations */

#include "console.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Events for pd_interrupt_handler_task */
#define PD_PROCESS_INTERRUPT  BIT(0)

/*
 * Theoretically, we may need to support up to 480 USB-PD packets per second for
 * intensive operations such as FW update over PD. This value has tested well
 * preventing watchdog resets with a single bad port partner plugged in.
 */
#define ALERT_STORM_MAX_COUNT   480
#define ALERT_STORM_INTERVAL    SECOND

static uint8_t pd_int_task_id[CONFIG_USB_PD_PORT_MAX_COUNT];

void schedule_deferred_pd_interrupt(const int port)
{
	task_set_event(pd_int_task_id[port], PD_PROCESS_INTERRUPT, 0);
}

/*
 * Main task entry point that handles PD interrupts for a single port
 *
 * @param p The PD port number for which to handle interrupts (pointer is
 * reinterpreted as an integer directly).
 */
void pd_interrupt_handler_task(void *p)
{
	const int port = (int) ((intptr_t) p);
	const int port_mask = (PD_STATUS_TCPC_ALERT_0 << port);
	struct {
		int count;
		timestamp_t time;
	} storm_tracker[CONFIG_USB_PD_PORT_MAX_COUNT] = {};

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	/*
	 * If port does not exist, return
	 */
	if (port >= board_get_usb_pd_port_count())
		return;

	pd_int_task_id[port] = task_get_current();

	while (1) {
		const int evt = task_wait_event(-1);

		if (evt & PD_PROCESS_INTERRUPT) {
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
				timestamp_t now;

				tcpc_alert(port);

				now = get_time();
				if (timestamp_expired(storm_tracker[port].time,
						      &now)) {
					/* Reset timer into future */
					storm_tracker[port].time.val =
						now.val + ALERT_STORM_INTERVAL;

					/*
					 * Start at 1 since we are processing an
					 * interrupt right now
					 */
					storm_tracker[port].count = 1;
				} else if (++storm_tracker[port].count >
							ALERT_STORM_MAX_COUNT) {
					CPRINTS("C%d: Interrupt storm detected."
						" Disabling port temporarily",
						port);

					pd_set_suspend(port, 1);
					pd_deferred_resume(port);
				}
			}
		}
	}
}
