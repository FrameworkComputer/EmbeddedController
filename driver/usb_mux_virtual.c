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
#include "util.h"

static mux_state_t virtual_mux_state[CONFIG_USB_PD_PORT_COUNT];
static int hpd_irq_state[CONFIG_USB_PD_PORT_COUNT];

static int virtual_init(int port)
{
	return EC_SUCCESS;
}

/*
 * Set the state of our 'virtual' mux. The EC does not actually control this
 * mux, so update the desired state, then notify the host of the update.
 */
static int virtual_set_mux(int port, mux_state_t mux_state)
{
	if (virtual_mux_state[port] != mux_state) {
		virtual_mux_state[port] = mux_state;
		host_set_single_event(EC_HOST_EVENT_USB_MUX);
	}
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
	*mux_state |= hpd_irq_state[port] ? USB_PD_MUX_HPD_IRQ : 0;

	return EC_SUCCESS;
}

void virtual_hpd_update(int port, int hpd_lvl, int hpd_irq)
{
	hpd_irq_state[port] = hpd_irq;
	if (hpd_irq)
		host_set_single_event(EC_HOST_EVENT_USB_MUX);
}

const struct usb_mux_driver virtual_usb_mux_driver = {
	.init = virtual_init,
	.set = virtual_set_mux,
	.get = virtual_get_mux,
};
