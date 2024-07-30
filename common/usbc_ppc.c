/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB-C Power Path Controller Common Code */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#ifndef TEST_LEGACY_BUILD
/*
 * We limit the CPRINTF/S invocations to all builds that are not
 * legacy test builds because they dont build otherwise.
 */
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

int ppc_prints(const char *string, int port)
{
#if !defined(TEST_LEGACY_BUILD) && defined(CONFIG_USBC_PPC_LOGGING)
	CPRINTS("ppc p%d %s", port, string);
#endif /* !defined(TEST_LEGACY_BUILD) && defined(CONFIG_USBC_PPC_LOGGING) */
	return 0;
}

int ppc_err_prints(const char *string, int port, int error)
{
#if !defined(TEST_LEGACY_BUILD) && defined(CONFIG_USBC_PPC_LOGGING)
	CPRINTS("ppc p%d %s (%d)", port, string, error);
#endif /* !defined(TEST_LEGACY_BUILD) && defined(CONFIG_USBC_PPC_LOGGING) */
	return 0;
}

__overridable bool board_port_has_ppc(int port)
{
	return true;
}

/* Simple wrappers to dispatch to the drivers. */

int ppc_init(int port)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if (!board_port_has_ppc(port))
		return EC_SUCCESS;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->init) {
		rv = ppc->drv->init(port);
		if (rv)
			ppc_err_prints("init failed!", port, rv);
		else
			ppc_prints("init'd.", port);
	}

	return rv;
}

test_mockable int ppc_is_sourcing_vbus(int port)
{
	int rv = 0;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return 0;
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

	if (!board_port_has_ppc(port))
		return EC_SUCCESS;

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

	if (!board_port_has_ppc(port))
		return EC_SUCCESS;

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

#ifdef CONFIG_USBC_PPC_SBU
int ppc_set_sbu(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if (!board_port_has_ppc(port))
		return EC_SUCCESS;

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

	if (!board_port_has_ppc(port))
		return EC_SUCCESS;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->set_vconn)
		rv = ppc->drv->set_vconn(port, enable);

	return rv;
}
#endif

int ppc_dev_is_connected(int port, enum ppc_device_role dev)
{
	int rv = EC_SUCCESS;
	const struct ppc_config_t *ppc;

	if (!board_port_has_ppc(port))
		return EC_SUCCESS;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];
	if (ppc->drv->dev_is_connected)
		rv = ppc->drv->dev_is_connected(port, dev);

	if (rv)
		CPRINTS("%s(%d) ppc->drv error %d!", __func__, port, rv);

	return rv;
}

test_mockable int ppc_vbus_sink_enable(int port, int enable)
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

	ppc = &ppc_chips[port];
	if (ppc->drv->vbus_source_enable)
		rv = ppc->drv->vbus_source_enable(port, enable);

	return rv;
}

#ifdef CONFIG_USB_PD_FRS_PPC
int ppc_set_frs_enable(int port, int enable)
{
	int rv = EC_ERROR_UNIMPLEMENTED;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}

	ppc = &ppc_chips[port];

	if (ppc->drv->set_frs_enable)
		rv = ppc->drv->set_frs_enable(port, enable);

	return rv;
}
#endif /* defined(CONFIG_USB_PD_FRS_PPC) */

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
int ppc_is_vbus_present(int port)
{
	int rv = 0;
	const struct ppc_config_t *ppc;

	if ((port < 0) || (port >= ppc_cnt)) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return 0;
	}

	ppc = &ppc_chips[port];

	if (ppc->drv->is_vbus_present)
		rv = ppc->drv->is_vbus_present(port);

	return rv;
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */

#ifdef CONFIG_CMD_PPC_DUMP
static int command_ppc_dump(int argc, const char **argv)
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
