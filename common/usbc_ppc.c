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

int ppc_is_sourcing_vbus(int port)
{
	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return 0;
	}

	return ppc_chips[port].drv->is_sourcing_vbus(port);
}

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

static void ppc_init(void)
{
	int i;
	int rv;

	for (i = 0; i < ppc_cnt; i++) {
		rv = ppc_chips[i].drv->init(i);
		if (rv)
			CPRINTS("p%d: PPC init failed! (%d)", i, rv);
		else
			CPRINTS("p%d: PPC init'd.", i);
	}
}
DECLARE_HOOK(HOOK_INIT, ppc_init, HOOK_PRIO_INIT_I2C + 1);

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
