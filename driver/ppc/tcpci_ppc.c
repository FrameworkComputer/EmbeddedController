/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB-C Power Path Controller via TCPCI conformant TCPC */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/tcpci.h"
#include "usbc_ppc.h"

#define TCPCI_PPC_FLAGS_SOURCE_ENABLED BIT(0)

static atomic_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

static int tcpci_ppc_is_sourcing_vbus(int port)
{
	return (flags[port] & TCPCI_PPC_FLAGS_SOURCE_ENABLED);
}

static int tcpci_ppc_vbus_source_enable(int port, int enable)
{
	RETURN_ERROR(tcpci_tcpm_set_src_ctrl(port, enable));

	if (enable)
		atomic_or(&flags[port], TCPCI_PPC_FLAGS_SOURCE_ENABLED);
	else
		atomic_clear_bits(&flags[port], TCPCI_PPC_FLAGS_SOURCE_ENABLED);

	/*
	 * Since the VBUS state could be changing here, need to wake the
	 * USB_CHG_N task so that BC 1.2 detection will be triggered.
	 */
	if (IS_ENABLED(CONFIG_USB_CHARGER) &&
	    IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_PPC))
		usb_charger_vbus_change(port, enable);

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int tcpci_is_vbus_present(int port)
{
	int status;

	if (tcpci_tcpm_get_power_status(port, &status))
		return 0;

	return !!(status & TCPC_REG_POWER_STATUS_VBUS_PRES);
}
#endif

static int tcpci_ppc_discharge_vbus(int port, int enable)
{
	tcpci_tcpc_discharge_vbus(port, enable);

	return EC_SUCCESS;
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int tcpci_ppc_set_polarity(int port, int polarity)
{
	return tcpci_tcpm_set_polarity(port, polarity);
}
#endif

static int tcpci_ppc_init(int port)
{
	atomic_clear(&flags[port]);

	return EC_SUCCESS;
}

const struct ppc_drv tcpci_ppc_drv = {
	.init = &tcpci_ppc_init,
	.is_sourcing_vbus = tcpci_ppc_is_sourcing_vbus,
	.vbus_sink_enable = tcpci_tcpm_set_snk_ctrl,
	.vbus_source_enable = tcpci_ppc_vbus_source_enable,

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = tcpci_is_vbus_present,
#endif

	.discharge_vbus = tcpci_ppc_discharge_vbus,

#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = tcpci_ppc_set_polarity,
#endif
};
