/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cypress CCGXXF PD chip driver source
 */

#include "ccgxxf.h"
#include "console.h"
#include "tcpm/tcpci.h"

#ifdef CONFIG_USB_PD_TCPM_SBU
static int ccgxxf_tcpc_set_sbu(int port, bool enable)
{
	return tcpc_write(port, CCGXXF_REG_SBU_MUX_CTL, enable);
}
#endif

#ifdef CONFIG_CMD_TCPC_DUMP
static void ccgxxf_dump_registers(int port)
{
	int fw_ver, fw_build;

	tcpc_dump_std_registers(port);

	/* Get the F/W version and build ID */
	if (!tcpc_read16(port, CCGXXF_REG_FW_VERSION, &fw_ver) &&
		!tcpc_read16(port, CCGXXF_REG_FW_VERSION_BUILD, &fw_build)) {
		ccprintf("  FW_VERSION(build.major.minor)        = %d.%d.%d\n",
			fw_build & 0xFF, (fw_ver >> 8) & 0xFF, fw_ver & 0xFF);
	}
}
#endif

const struct tcpm_drv ccgxxf_tcpm_drv = {
	.init			= &tcpci_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable	= &tcpci_tcpm_sop_prime_enable,
#endif
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
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_PPC
	.get_snk_ctrl		= &tcpci_tcpm_get_snk_ctrl,
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.get_src_ctrl		= &tcpci_tcpm_get_src_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPM_SBU
	.set_sbu		= &ccgxxf_tcpc_set_sbu,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
	.set_bist_test_mode	= &tcpci_set_bist_test_mode,
#ifdef CONFIG_CMD_TCPC_DUMP
	.dump_registers		= &ccgxxf_dump_registers,
#endif
};
