/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * AOZ1380 USB-C Power Path Controller
 *
 * This is a basic TCPM controlled PPC driver.  It could easily be
 * renamed and repurposed to be generic, if there are other TCPM
 * controlled PPC chips that are similar to the AOZ1380
 */

#include "common.h"
#include "console.h"
#include "driver/ppc/aoz1380.h"
#include "hooks.h"
#include "system.h"
#include "tcpm.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "usbc_ppc.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint32_t irq_pending; /* Bitmask of ports signaling an interrupt. */

#define AOZ1380_FLAGS_SOURCE_ENABLED    BIT(0)
static uint8_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

static int aoz1380_init(int port)
{
	flags[port] = 0;

	return EC_SUCCESS;
}

static int aoz1380_vbus_sink_enable(int port, int enable)
{
	int rv;

	rv = tcpm_set_snk_ctrl(port, enable);

	return rv;
}

static int aoz1380_vbus_source_enable(int port, int enable)
{
	int rv;

	rv = tcpm_set_src_ctrl(port, enable);
	if (rv)
		return rv;

	if (enable)
		flags[port] |= AOZ1380_FLAGS_SOURCE_ENABLED;
	else
		flags[port] &= ~AOZ1380_FLAGS_SOURCE_ENABLED;

	return rv;
}

static int aoz1380_is_sourcing_vbus(int port)
{
	return flags[port] & AOZ1380_FLAGS_SOURCE_ENABLED;
}

static int aoz1380_set_vbus_source_current_limit(int port,
						 enum tcpc_rp_value rp)
{
	return board_aoz1380_set_vbus_source_current_limit(port, rp);
}

/*
 * AOZ1380 Interrupt Handler
 *
 * This device only has a single over current/temperature interrupt.
 * TODO(b/141939343) Determine how to clear the interrupt
 * TODO(b/142076004) Test this to verify we shut off vbus current
 */
static void aoz1380_handle_interrupt(int port)
{
	/*
	 * This is a over current/temperature condition
	 */
	CPRINTS("C%d: PPC detected Vbus overcurrent/temperature!", port);
	pd_handle_overcurrent(port);
}

static void aoz1380_irq_deferred(void)
{
	int i;
	uint32_t pending = atomic_read_clear(&irq_pending);

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		if (BIT(i) & pending)
			aoz1380_handle_interrupt(i);
}
DECLARE_DEFERRED(aoz1380_irq_deferred);

void aoz1380_interrupt(int port)
{
	atomic_or(&irq_pending, BIT(port));
	hook_call_deferred(&aoz1380_irq_deferred_data, 0);
}

const struct ppc_drv aoz1380_drv = {
	.init = &aoz1380_init,
	.is_sourcing_vbus = &aoz1380_is_sourcing_vbus,
	.vbus_sink_enable = &aoz1380_vbus_sink_enable,
	.vbus_source_enable = &aoz1380_vbus_source_enable,
	.set_vbus_source_current_limit =
		&aoz1380_set_vbus_source_current_limit,
};
