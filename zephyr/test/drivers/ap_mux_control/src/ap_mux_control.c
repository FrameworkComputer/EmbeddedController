/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "usb_mux.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

static void ap_mux_control_before(void *data)
{
	/* Set chipset on to ensure muxes are "powered" */
	test_set_chipset_to_s0();

	/*
	 * Set all muxes to NONE to begin with and give time for the USB_MUX
	 * task to process them.
	 */
	usb_mux_set(USBC_PORT_C0, USB_PD_MUX_NONE, USB_SWITCH_CONNECT, 0);
	k_sleep(K_SECONDS(1));

	/* And test the assumption that setting NONE worked */
	zassume_equal(usb_mux_get(USBC_PORT_C0), USB_PD_MUX_NONE,
		      "Failed to set mux to initial state");
}

static void ap_mux_control_after(void *data)
{
	/*
	 * Set all muxes to NONE now that we're done and give time for the
	 * USB_MUX task to process them.
	 */
	usb_mux_set(USBC_PORT_C0, USB_PD_MUX_NONE, USB_SWITCH_CONNECT, 0);
	k_sleep(K_SECONDS(1));
}

ZTEST_SUITE(ap_mux_control, drivers_predicate_post_main, NULL,
	    ap_mux_control_before, ap_mux_control_after, NULL);

ZTEST(ap_mux_control, test_set_muxes)
{
	struct ec_response_typec_status status;
	struct typec_usb_mux_set mux_set;
	uint32_t port_events;
	int index;
	uint8_t set_mode = USB_PD_MUX_DOCK;

	/* Test setting both mux indexes and receiving their events */
	/* TODO(b/239457738): Loop counter should come from device tree */
	for (index = 0; index < 2; index++) {
		mux_set.mux_index = index;
		mux_set.mux_flags = set_mode;

		host_cmd_typec_control_usb_mux_set(USBC_PORT_C0, mux_set);

		/* Give the task processing time */
		k_sleep(K_SECONDS(1));

		/*
		 * TODO(b/239460181): The "AP" should receive
		 * EC_HOST_EVENT_PD_MCU
		 */

		/* We should see the right index's event set on the port */
		status = host_cmd_typec_status(USBC_PORT_C0);
		port_events = index ? PD_STATUS_EVENT_MUX_1_SET_DONE :
				      PD_STATUS_EVENT_MUX_0_SET_DONE;
		zassert_true(status.events & port_events, "Port event missing");

		/* Clear this mux's event */
		host_cmd_typec_control_clear_events(USBC_PORT_C0, port_events);
	}

	/*
	 * Verify our mux mode is reported as set, and that our mux events are
	 * cleared out
	 */
	status = host_cmd_typec_status(USBC_PORT_C0);
	port_events = PD_STATUS_EVENT_MUX_0_SET_DONE |
		      PD_STATUS_EVENT_MUX_1_SET_DONE;
	zassert_false(status.events & port_events, "Port events still set");
	zassert_equal(status.mux_state, set_mode,
		      "Mux set to unexpected state");
}
