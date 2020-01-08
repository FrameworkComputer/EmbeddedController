/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas RAA489000 TCPC driver
 */

#include "i2c.h"
#include "raa489000.h"
#include "tcpci.h"
#include "tcpm.h"

int raa489000_tcpm_set_cc(int port, int pull)
{
	int rv;

	rv = tcpci_tcpm_set_cc(port, pull);
	if (rv)
		return rv;

	/* TCPM should set RDOE to 1 after setting Rp */
	if (pull == TYPEC_CC_RP)
		rv = tcpc_update16(port, RAA489000_TYPEC_SETTING1,
				   RAA489000_SETTING1_RDOE, MASK_SET);

	return rv;
}

/* RAA489000 is a TCPCI compatible port controller */
const struct tcpm_drv raa489000_tcpm_drv = {
	.init                   = &tcpci_tcpm_init,
	.release                = &tcpci_tcpm_release,
	.get_cc                 = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level         = &tcpci_tcpm_get_vbus_level,
#endif
	.select_rp_value        = &tcpci_tcpm_select_rp_value,
	.set_cc                 = &raa489000_tcpm_set_cc,
	.set_polarity           = &tcpci_tcpm_set_polarity,
	.set_vconn              = &tcpci_tcpm_set_vconn,
	.set_msg_header         = &tcpci_tcpm_set_msg_header,
	.set_rx_enable          = &tcpci_tcpm_set_rx_enable,
	.get_message_raw        = &tcpci_tcpm_get_message_raw,
	.transmit               = &tcpci_tcpm_transmit,
	.tcpc_alert             = &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus    = &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle             = &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info          = &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode   = &tcpci_enter_low_power_mode,
#endif
};

