/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared USB-C policy for DragonEgg boards */

#include "charge_manager.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "system.h"
#include "tcpci.h"
#include "tcpm.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_check_vconn_swap(int port)
{
	/* Only allow vconn swap if pp5000_A rail is enabled */
	return gpio_get_level(GPIO_EN_PP5000);
}

__override void pd_execute_data_swap(int port,
				     enum pd_data_role data_role)
{
	/* On DragonEgg, only the first port can act as OTG */
	if (port == 0)
		gpio_set_level(GPIO_CHG_VAP_OTG_EN, (data_role == PD_ROLE_UFP));
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
int pd_snk_is_vbus_provided(int port)
{
	/*
	 * TODO(b/112661747): Until per port VBUS detection methods are
	 * supported, DragonEgg needs to have CONFIG_USB_PD_VBUS_DETECT_PPC
	 * defined, but the nx20p3481 PPC on port 2 does not support VBUS
	 * detection. In the meantime, check specifically for port 2, and rely
	 * on the TUSB422 TCPC for VBUS status. Note that the tcpm method can't
	 * be called directly here as it's not supported unless
	 * CONFIG_USB_PD_VBUS_DETECT_TCPC is defined.
	 */
	int reg;

	if (port == 2) {
		if (tcpc_read(port, TCPC_REG_POWER_STATUS, &reg))
			return 0;
		return reg & TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
	}
	return ppc_is_vbus_present(port);
}
#endif

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}
