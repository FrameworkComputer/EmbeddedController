/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager for TI TUSB422 Port Controller */

#include "common.h"
#include "tusb422.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_pd.h"

#ifndef CONFIG_USB_PD_TCPM_TCPCI
#error "TUSB422 is using a standard TCPCI interface"
#error "Please upgrade your board configuration"

#endif

#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) && \
	!defined(CONFIG_USB_PD_TCPC_LOW_POWER)
#error "TUSB422 driver requires CONFIG_USB_PD_TCPC_LOW_POWER if"
#error "CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE is enabled"
#endif

static int tusb422_tcpci_tcpm_init(int port)
{
	int rv = tcpci_tcpm_init(port);

	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE)) {
		/*
		 * When dual role auto toggle is enabled, the TUSB422 needs
		 * auto discharge disconnect enabled so that the CC state
		 * is detected correctly.
		 * Without this, the CC lines get stuck in the SRC.Open state
		 * after updating the ROLE Control register on a device connect.
		 */
		tusb422_tcpm_drv.tcpc_enable_auto_discharge_disconnect(port, 1);
	}

	/*
	 * VBUS detection is supposed to be enabled by default, however the
	 * TUSB422 has this disabled following reset.
	 */
	/* Enable VBUS detection */
	return tcpc_write16(port, TCPC_REG_COMMAND, 0x33);
}

const struct tcpm_drv tusb422_tcpm_drv = {
	.init			= &tusb422_tcpci_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
				  &tcpci_tcpc_enable_auto_discharge_disconnect,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
};
