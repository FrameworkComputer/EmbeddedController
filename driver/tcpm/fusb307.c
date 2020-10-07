/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager for Fairchild's FUSB307 */

#include "console.h"
#include "fusb307.h"
#include "hooks.h"
#include "task.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int fusb307_power_supply_reset(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, 0x66);
}

static int fusb307_tcpm_init(int port)
{
	int rv;

	rv = tcpci_tcpm_init(port);

	rv = tcpci_set_role_ctrl(port, 1, TYPEC_RP_USB, TYPEC_CC_RD);
	pd_set_dual_role(port, PD_DRP_TOGGLE_ON);

	return rv;
}

int fusb307_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	int rv;
	enum tcpc_cc_voltage_status cc1, cc2;

	rv = tcpci_tcpm_set_polarity(port, polarity);

	tcpm_get_cc(port, &cc1, &cc2);
	if (cc1) {
		if (pd_get_power_role(port) == PD_ROLE_SINK) {
			int role = TCPC_REG_ROLE_CTRL_SET(0,
			tcpci_get_cached_rp(port), TYPEC_CC_RD, TYPEC_CC_OPEN);

			tcpc_write(port, TCPC_REG_ROLE_CTRL, role);
		} else {
			int role = TCPC_REG_ROLE_CTRL_SET(0,
			tcpci_get_cached_rp(port), TYPEC_CC_RP, TYPEC_CC_OPEN);

			tcpc_write(port, TCPC_REG_ROLE_CTRL, role);
		}
	} else if (cc2) {
		if (pd_get_power_role(port) == PD_ROLE_SINK) {
			int role = TCPC_REG_ROLE_CTRL_SET(0,
			tcpci_get_cached_rp(port), TYPEC_CC_OPEN, TYPEC_CC_RD);

			tcpc_write(port, TCPC_REG_ROLE_CTRL, role);
		} else {
			int role = TCPC_REG_ROLE_CTRL_SET(0,
			tcpci_get_cached_rp(port), TYPEC_CC_OPEN, TYPEC_CC_RP);

			tcpc_write(port, TCPC_REG_ROLE_CTRL, role);
		}
	} else {
		if (pd_get_power_role(port) == PD_ROLE_SINK)
			tcpci_tcpm_set_cc(port, TYPEC_CC_RD);
		else
			tcpci_tcpm_set_cc(port, TYPEC_CC_RP);
	}

	return rv;
}

const struct tcpm_drv fusb307_tcpm_drv = {
	.init			= &fusb307_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &fusb307_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tcpci_tcpc_alert,
	.tcpc_enable_auto_discharge_disconnect =
				  &tcpci_tcpc_enable_auto_discharge_disconnect,
	.get_chip_info		= &tcpci_get_chip_info,
#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE)
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
};

