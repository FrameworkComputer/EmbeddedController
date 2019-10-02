/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB-C Power Path Controller Common Code */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "usbc_ppc.h"
#include "util.h"

#ifndef TEST_BUILD
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(args...)
#define CPRINTS(args...)
#endif

/*
 * A per-port table that indicates how many VBUS overcurrent events have
 * occurred.  This table is cleared after detecting a physical disconnect of the
 * sink.
 */
static uint8_t oc_event_cnt_tbl[CONFIG_USB_PD_PORT_MAX_COUNT];

static uint32_t connected_ports;

/* Simple wrappers to dispatch to the drivers. */

int ppc_init(int port)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->init) {
		rv = ppc->drv->init(port);
		if (rv)
			CPRINTS("p%d: PPC init failed! (%d)", port, rv);
		else
			CPRINTS("p%d: PPC init'd.", port);
	}

	return rv;
}

int ppc_add_oc_event(int port)
{
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	oc_event_cnt_tbl[port]++;

	/* The port overcurrented, so don't clear it's OC events. */
	atomic_clear(&connected_ports, 1 << port);

	if (oc_event_cnt_tbl[port] >= PPC_OC_CNT_THRESH)
		CPRINTS("C%d: OC event limit reached! "
			"Source path disabled until physical disconnect.",
			port);
	return EC_SUCCESS;
}

static void clear_oc_tbl(void)
{
	int port;

	for (port = 0; port < ppc_cnt; port++)
		/*
		 * Only clear the table if the port partner is no longer
		 * attached after debouncing.
		 */
		if ((!(BIT(port) & connected_ports)) &&
		    oc_event_cnt_tbl[port]) {
			oc_event_cnt_tbl[port] = 0;
			CPRINTS("C%d: OC events cleared", port);
		}
}
DECLARE_DEFERRED(clear_oc_tbl);

int ppc_clear_oc_event_counter(int port)
{
	if ((port < 0) || (port >= ppc_cnt)) {
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
				   PPC_OC_COOLDOWN_DELAY_US);
	}
	return EC_SUCCESS;
}

int ppc_is_sourcing_vbus(int port)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->is_sourcing_vbus)
		rv = ppc->drv->is_sourcing_vbus(port);

	return rv;
}

#ifdef CONFIG_USBC_PPC_POLARITY
int ppc_set_polarity(int port, int polarity)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->set_polarity)
		rv = ppc->drv->set_polarity(port, polarity);

	return rv;
}
#endif

int ppc_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->set_vbus_source_current_limit)
		rv = ppc->drv->set_vbus_source_current_limit(port, rp);

	return rv;
}

int ppc_discharge_vbus(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->discharge_vbus)
		rv = ppc->drv->discharge_vbus(port, enable);

	return rv;
}

int ppc_is_port_latched_off(int port)
{
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	return oc_event_cnt_tbl[port] >= PPC_OC_CNT_THRESH;
}

#ifdef CONFIG_USBC_PPC_SBU
int ppc_set_sbu(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->set_sbu)
		rv = ppc->drv->set_sbu(port, enable);

	return rv;
}
#endif /* defined(CONFIG_USBC_PPC_SBU) */

#ifdef CONFIG_USBC_PPC_VCONN
int ppc_set_vconn(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Check our OC event counter.  If we've exceeded our threshold, then
	 * let's latch our source path off to prevent continuous cycling.  When
	 * the PD state machine detects a disconnection on the CC lines, we will
	 * reset our OC event counter.
	 */
	if (enable && ppc_is_port_latched_off(port))
		return EC_ERROR_ACCESS_DENIED;

	ppc = &ppc_chips[port];
	if (ppc->drv->set_vconn)
		rv = ppc->drv->set_vconn(port, enable);

	return rv;
}
#endif

void ppc_sink_is_connected(int port, int is_connected)
{
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return;
	}

	if (is_connected)
		atomic_or(&connected_ports, 1 << port);
	else
		atomic_clear(&connected_ports, 1 << port);
}

int ppc_vbus_sink_enable(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->vbus_sink_enable)
		rv = ppc->drv->vbus_sink_enable(port, enable);

	return rv;
}

int ppc_enter_low_power_mode(int port)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->enter_low_power_mode)
		rv = ppc->drv->enter_low_power_mode(port);

	return rv;
}

int ppc_vbus_source_enable(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Check our OC event counter.  If we've exceeded our threshold, then
	 * let's latch our source path off to prevent continuous cycling.  When
	 * the PD state machine detects a disconnection on the CC lines, we will
	 * reset our OC event counter.
	 */
	if (enable && ppc_is_port_latched_off(port))
		return EC_ERROR_ACCESS_DENIED;

	ppc = &ppc_chips[port];
	if (ppc->drv->vbus_source_enable)
		rv = ppc->drv->vbus_source_enable(port, enable);

	return rv;
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
int ppc_is_vbus_present(int port)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];

	if (ppc->drv->is_vbus_present)
		rv = ppc->drv->is_vbus_present(port);

	return rv;
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */

#ifdef CONFIG_CMD_PPC_DUMP
static int command_ppc_dump(int argc, char **argv)
{
	int port;
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = atoi(argv[1]);
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->reg_dump)
		rv = ppc->drv->reg_dump(port);

	return rv;
}
DECLARE_CONSOLE_COMMAND(ppc_dump, command_ppc_dump, "<Type-C port>",
			"dump the PPC regs");
#endif /* defined(CONFIG_CMD_PPC_DUMP) */
