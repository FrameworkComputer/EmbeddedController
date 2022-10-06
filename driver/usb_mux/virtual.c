/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Virtual USB mux driver for host-controlled USB muxes.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "util.h"

/*
 * USB PD protocol configures the USB & DP mux state and USB PD policy
 * configures the HPD mux state. Both states are independent of each other
 * may differ when the PD role changes when in dock mode.
 */
#define USB_PD_MUX_HPD_STATE (USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ)
#define USB_PD_MUX_USB_DP_STATE                                \
	(USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |      \
	 USB_PD_MUX_POLARITY_INVERTED | USB_PD_MUX_SAFE_MODE | \
	 USB_PD_MUX_TBT_COMPAT_ENABLED | USB_PD_MUX_USB4_ENABLED)

static mux_state_t virtual_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static inline void virtual_mux_update_state(int port, mux_state_t mux_state,
					    bool *ack_required)
{
	mux_state_t previous_mux_state = virtual_mux_state[port];

	virtual_mux_state[port] = mux_state;

	/*
	 * Initialize ack_required to false to start, and set on necessary
	 * conditions
	 */
	*ack_required = false;

	if (!IS_ENABLED(CONFIG_HOSTCMD_EVENTS))
		return;

	host_set_single_event(EC_HOST_EVENT_USB_MUX);

	if (!IS_ENABLED(CONFIG_USB_MUX_AP_ACK_REQUEST))
		return;

	/*
	 * EC waits for the ACK from kernel indicating that TCSS Mux
	 * configuration is completed. This mechanism is implemented for
	 * entering, exiting the safe mode and entering the disconnect mode
	 * This is needed to remove timing senstivity between BB retimer and
	 * TCSS Mux to allow better synchronization between them and thereby
	 * remain in the same state for achieving proper safe state
	 * terminations.
	 *
	 * Note the AP will only ACK if the mux state changed in some way.
	 */
	if (previous_mux_state != mux_state)
		*ack_required = true;
}

static int virtual_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

/*
 * Set the state of our 'virtual' mux. The EC does not actually control this
 * mux, so update the desired state, then notify the host of the update.
 */
static int virtual_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required)
{
	int port = me->usb_port;
	mux_state_t new_mux_state;

	/*
	 * Current USB & DP mux status + existing HPD related mux status if DP
	 * is still active.  Otherwise, don't preserve HPD state.
	 */
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		new_mux_state =
			(mux_state & ~USB_PD_MUX_HPD_STATE) |
			(virtual_mux_state[port] & USB_PD_MUX_HPD_STATE);
	else
		new_mux_state = mux_state;

	virtual_mux_update_state(port, new_mux_state, ack_required);

	return EC_SUCCESS;
}

/*
 * Get the state of our 'virtual' mux. Since we the EC does not actually
 * control this mux, and the EC has no way of knowing its actual status,
 * we return the desired state here.
 */
static int virtual_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int port = me->usb_port;

	*mux_state = virtual_mux_state[port];

	return EC_SUCCESS;
}

void virtual_hpd_update(const struct usb_mux *me, mux_state_t hpd_state,
			bool *ack_required)
{
	int port = me->usb_port;

	/* Current HPD related mux status + existing USB & DP mux status */
	mux_state_t new_mux_state =
		hpd_state | (virtual_mux_state[port] & USB_PD_MUX_USB_DP_STATE);

	virtual_mux_update_state(port, new_mux_state, ack_required);
}

const struct usb_mux_driver virtual_usb_mux_driver = {
	.init = virtual_init,
	.set = virtual_set_mux,
	.get = virtual_get_mux,
};
