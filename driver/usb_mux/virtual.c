/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Virtual USB mux driver for host-controlled USB muxes.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "usb_mux.h"
#include "usb_retimer.h"
#include "util.h"

/*
 * USB PD protocol configures the USB & DP mux state and USB PD policy
 * configures the HPD mux state. Both states are independent of each other
 * may differ when the PD role changes when in dock mode.
 */
#define USB_PD_MUX_HPD_STATE	(USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ)
#define USB_PD_MUX_USB_DP_STATE	(USB_PD_MUX_USB_ENABLED | \
			USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED | \
			USB_PD_MUX_SAFE_MODE)

static mux_state_t virtual_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static inline void virtual_mux_update_state(int port, mux_state_t mux_state)
{
	if (virtual_mux_state[port] != mux_state) {
		virtual_mux_state[port] = mux_state;
#ifdef CONFIG_USB_PD_RETIMER
		if (retimer_set_state(port, mux_state))
			return;
#endif
		host_set_single_event(EC_HOST_EVENT_USB_MUX);
	}
}

static int virtual_init(int port)
{
#ifdef CONFIG_USB_PD_RETIMER
	return retimer_init(port);
#else
	return EC_SUCCESS;
#endif
}

/*
 * Set the state of our 'virtual' mux. The EC does not actually control this
 * mux, so update the desired state, then notify the host of the update.
 */
static int virtual_set_mux(int port, mux_state_t mux_state)
{
	/* Current USB & DP mux status + existing HPD related mux status */
	mux_state_t new_mux_state = (mux_state & ~USB_PD_MUX_HPD_STATE) |
		(virtual_mux_state[port] & USB_PD_MUX_HPD_STATE);

	virtual_mux_update_state(port, new_mux_state);

	return EC_SUCCESS;
}

/*
 * Get the state of our 'virtual' mux. Since we the EC does not actually
 * control this mux, and the EC has no way of knowing its actual status,
 * we return the desired state here.
 */
static int virtual_get_mux(int port, mux_state_t *mux_state)
{
	*mux_state = virtual_mux_state[port];

	return EC_SUCCESS;
}

void virtual_hpd_update(int port, int hpd_lvl, int hpd_irq)
{
	/* Current HPD related mux status + existing USB & DP mux status */
	mux_state_t new_mux_state = (hpd_lvl ? USB_PD_MUX_HPD_LVL : 0) |
			(hpd_irq ? USB_PD_MUX_HPD_IRQ : 0) |
			(virtual_mux_state[port] & USB_PD_MUX_USB_DP_STATE);

	virtual_mux_update_state(port, new_mux_state);
}

const struct usb_mux_driver virtual_usb_mux_driver = {
	.init = virtual_init,
	.set = virtual_set_mux,
	.get = virtual_get_mux,
#ifdef CONFIG_USB_PD_RETIMER
	.enter_low_power_mode = retimer_low_power_mode,
#endif
};
