/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB-C Power Path Controller Common Code */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Simple wrappers to dispatch to the drivers. */

int ppc_init(int port)
{
	int rv;

	if (port >= ppc_cnt)
		return EC_ERROR_INVAL;

	rv = ppc_chips[port].drv->init(port);
	if (rv)
		CPRINTS("p%d: PPC init failed! (%d)", port, rv);
	else
		CPRINTS("p%d: PPC init'd.", port);

	return rv;
}

int ppc_is_sourcing_vbus(int port)
{
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return 0;
	}

	return ppc_chips[port].drv->is_sourcing_vbus(port);
}

#ifdef CONFIG_USBC_PPC_POLARITY
int ppc_set_polarity(int port, int polarity)
{
	if ((port < 0) || (port >= ppc_cnt))
		return EC_ERROR_INVAL;

	return ppc_chips[port].drv->set_polarity(port, polarity);
}
#endif

int ppc_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if ((port < 0) || (port >= ppc_cnt))
		return EC_ERROR_INVAL;

	return ppc_chips[port].drv->set_vbus_source_current_limit(port, rp);
}

int ppc_discharge_vbus(int port, int enable)
{
	if ((port < 0) || (port >= ppc_cnt))
		return EC_ERROR_INVAL;

	return ppc_chips[port].drv->discharge_vbus(port, enable);
}

#ifdef CONFIG_USBC_PPC_VCONN
int ppc_set_vconn(int port, int enable)
{
	if ((port < 0) || (port >= ppc_cnt))
		return EC_ERROR_INVAL;

	return ppc_chips[port].drv->set_vconn(port, enable);
}
#endif

int ppc_vbus_sink_enable(int port, int enable)
{
	if ((port < 0) || (port >= ppc_cnt))
		return EC_ERROR_INVAL;

	return ppc_chips[port].drv->vbus_sink_enable(port, enable);
}

int ppc_vbus_source_enable(int port, int enable)
{
	if ((port < 0) || (port >= ppc_cnt))
		return EC_ERROR_INVAL;

	return ppc_chips[port].drv->vbus_source_enable(port, enable);
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
int ppc_is_vbus_present(int port)
{
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return 0;
	}

	return ppc_chips[port].drv->is_vbus_present(port);
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */


#ifdef CONFIG_CMD_PPC_DUMP
static int command_ppc_dump(int argc, char **argv)
{
	int port;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = atoi(argv[1]);
	if (port >= ppc_cnt)
		return EC_ERROR_PARAM1;

	return ppc_chips[port].drv->reg_dump(port);
}
DECLARE_CONSOLE_COMMAND(ppc_dump, command_ppc_dump, "<Type-C port>",
			"dump the PPC regs");
#endif /* defined(CONFIG_CMD_PPC_DUMP) */
