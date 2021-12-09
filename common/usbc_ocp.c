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
 * Number of times a port may overcurrent before we latch off the port until a
 * physical disconnect is detected.
 */
#define OCP_CNT_THRESH 3

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

int usbc_ocp_add_event(int port)
{
	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	oc_event_cnt_tbl[port]++;

	/* The port overcurrented, so don't clear it's OC events. */
	atomic_clear_bits(&snk_connected_ports, 1 << port);

	if (oc_event_cnt_tbl[port] >= OCP_CNT_THRESH)
		CPRINTS("C%d: OC event limit reached! "
			   "Source path disabled until physical disconnect.",
			   port);
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

	return oc_event_cnt_tbl[port] >= OCP_CNT_THRESH;
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
