/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB-C Overcurrent Protection Common Code */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usbc_ocp.h"
#include "util.h"

#ifndef TEST_BUILD
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(args...)
#define CPRINTS(args...)
#endif

/*
 * PD 3.1 Ver 1.3 7.1.7.1 Output Over Current Protection
 *
 * "After three consecutive over current events Source Shall go to
 * ErrorRecovery.
 *
 * Sources Should attempt to send a Hard Reset message when over
 * current protection engages followed by an Alert Message indicating
 * an OCP event once an Explicit Contract has been established.
 *
 * The Source Shall prevent continual system or port cycling if over
 * current protection continues to engage after initially resuming
 * either default operation or renegotiation. Latching off the port or
 * system is an acceptable response to recurring over current."
 *
 * Our policy will be first two OCPs -> hard reset
 * 3rd -> ErrorRecovery
 * 4th -> port latched off
 */
#define OCP_HR_CNT 2

#define OCP_MAX_CNT 4

/*
 * Number of seconds until a latched-off port is re-enabled for sourcing after
 * detecting a physical disconnect.
 */
#define OCP_COOLDOWN_DELAY_US (2 * SECOND)

/*
 * A per-port table that indicates how many VBUS overcurrent events have
 * occurred.  This table is cleared after detecting a physical disconnect of the
 * sink.
 */
static uint8_t oc_event_cnt_tbl[CONFIG_USB_PD_PORT_MAX_COUNT];

/* A flag for ports with sink device connected. */
static atomic_t snk_connected_ports;

static void clear_oc_tbl(void)
{
	int port;

	for (port = 0; port < board_get_usb_pd_port_count(); port++)
		/*
		 * Only clear the table if the port partner is no longer
		 * attached after debouncing.
		 */
		if ((!(BIT(port) & (uint32_t)snk_connected_ports)) &&
		    oc_event_cnt_tbl[port]) {
			oc_event_cnt_tbl[port] = 0;
			CPRINTS("C%d: OC events cleared", port);
		}
}
DECLARE_DEFERRED(clear_oc_tbl);

static atomic_t port_oc_reset_req;

static void re_enable_ports(void)
{
	uint32_t ports = atomic_clear(&port_oc_reset_req);

	while (ports) {
		int port = __fls(ports);

		ports &= ~BIT(port);

		/*
		 * Let the board know that the overcurrent is
		 * over since we've completed our recovery actions by now.
		 */
		board_overcurrent_event(port, 0);

		/* Queue up an Alert message for the partner */
		pd_send_alert_msg(port, ADO_OCP_EVENT);
	}
}
DECLARE_DEFERRED(re_enable_ports);


int usbc_ocp_add_event(int port)
{
	int delay = 0;

	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	/* Let the board specific code know about the OC event. */
	board_overcurrent_event(port, 1);

	oc_event_cnt_tbl[port]++;

	/* The port overcurrented, so don't clear it's OC events. */
	atomic_clear_bits(&snk_connected_ports, 1 << port);

	if (oc_event_cnt_tbl[port] >= OCP_MAX_CNT) {
		CPRINTS("C%d: OC event limit reached! "
			   "Source path disabled until physical disconnect.",
			   port);
		pd_power_supply_reset(port);
	} else if (oc_event_cnt_tbl[port] <= OCP_HR_CNT) {
		/*
		 * Hard reset for the first few offenses, sending an alert after
		 * at least the time we need to hard reset and make a new
		 * contract.
		 */
		pd_send_hard_reset(port);
		delay = PD_T_SRC_RECOVER + 100*MSEC;
	} else {
		/*
		 * ErrorRecovery must be performed past the third OCP event,
		 * queueing up the alert for after it completes and a new
		 * contract is in place
		 */
		pd_set_error_recovery(port);
		delay = PD_T_ERROR_RECOVERY + 100*MSEC;
	}

	if (delay) {
		atomic_or(&port_oc_reset_req, BIT(port));
		hook_call_deferred(&re_enable_ports_data, delay);
	}


	return EC_SUCCESS;
}


int usbc_ocp_clear_event_counter(int port)
{
	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	/*
	 * If we are clearing our event table in quick succession, we may be in
	 * an overcurrent loop where we are also detecting a disconnect on the
	 * CC pins.  Therefore, let's not clear it just yet and the let the
	 * limit be reached.  This way, we won't send the hard reset and
	 * actually detect the physical disconnect.
	 */
	if (oc_event_cnt_tbl[port]) {
		hook_call_deferred(&clear_oc_tbl_data,
				   OCP_COOLDOWN_DELAY_US);
	}
	return EC_SUCCESS;
}

int usbc_ocp_is_port_latched_off(int port)
{
	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return 0;
	}

	return oc_event_cnt_tbl[port] >= OCP_MAX_CNT;
}

void usbc_ocp_snk_is_connected(int port, bool connected)
{
	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return;
	}

	if (connected)
		atomic_or(&snk_connected_ports, 1 << port);
	else
		atomic_clear_bits(&snk_connected_ports, 1 << port);
}

__overridable void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Do nothing by default.  Boards with overcurrent GPIOs may override */
}
