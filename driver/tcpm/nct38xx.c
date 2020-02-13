/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager for Nuvoton NCT38XX. */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "ioexpander_nct38xx.h"
#include "nct38xx.h"
#include "task.h"
#include "tcpci.h"
#include "usb_common.h"

#if !defined(CONFIG_USB_PD_TCPM_TCPCI)
#error "NCT38XX is using part of standard TCPCI control"
#error "Please upgrade your board configuration"
#endif

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static int nct38xx_tcpm_init(int port)
{
	int rv = 0;
	int reg;

	rv = tcpci_tcpm_init(port);
		if (rv)
			return rv;

	/*
	 * Write to the CONTROL_OUT_EN register to enable:
	 * [6] - CONNDIREN : Connector direction indication output enable
	 * [2] - SNKEN     : VBUS sink enable output enable
	 * [0] - SRCEN     : VBUS source voltage enable output enable
	 */
	reg = NCT38XX_REG_CTRL_OUT_EN_SRCEN |
			NCT38XX_REG_CTRL_OUT_EN_SNKEN |
			NCT38XX_REG_CTRL_OUT_EN_CONNDIREN;

	rv = tcpc_write(port, NCT38XX_REG_CTRL_OUT_EN, reg);
	if (rv)
		return rv;

	/* Disable OVP */
	rv = tcpc_update8(port,
			  TCPC_REG_FAULT_CTRL,
			  TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS,
			  MASK_SET);
	if (rv)
		return rv;

	/* Enable VBus monitor and Disable FRS */
	rv = tcpc_update8(port,
			  TCPC_REG_POWER_CTRL,
			  (TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
			   TCPC_REG_POWER_CTRL_FRS_ENABLE),
			  MASK_CLR);
	if (rv)
		return rv;

	/* Set FRS direction for SNK detect, if FRS is enabled */
	if (IS_ENABLED(CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP)) {
		reg = TCPC_REG_DEV_CAP_2_SNK_FR_SWAP;
		rv = tcpc_write(port, TCPC_REG_DEV_CAP_2, reg);
		if (rv)
			return rv;

		reg = TCPC_REG_CONFIG_EXT_1_FR_SWAP_SNK_DIR;
		rv = tcpc_write(port, TCPC_REG_CONFIG_EXT_1, reg);
		if (rv)
			return rv;
	}

	/* Start VBus monitor */
	rv = tcpc_write(port, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_ENABLE_VBUS_DETECT);

	/*
	 * Enable the Vendor Define alert event only when the IO expander
	 * feature is defined
	 */
	if (IS_ENABLED(CONFIG_IO_EXPANDER_NCT38XX))
		rv |= tcpc_update16(port,
				    TCPC_REG_ALERT_MASK,
				    TCPC_REG_ALERT_VENDOR_DEF,
				    MASK_SET);

	return rv;
}

static void nct38xx_tcpc_alert(int port)
{
	int alert, rv;

	/*
	 * If IO expander feature is defined, read the ALERT register first to
	 * keep the status of Vendor Define bit. Otherwise, the status of ALERT
	 * register will be cleared after tcpci_tcpc_alert() is executed.
	 */
	if (IS_ENABLED(CONFIG_IO_EXPANDER_NCT38XX))
		rv = tcpc_read16(port, TCPC_REG_ALERT, &alert);

	/* Process normal TCPC ALERT event and clear status */
	tcpci_tcpc_alert(port);

	/*
	 * If IO expander feature is defined, check the Vendor Define bit to
	 * handle the IOEX IO's interrupt event
	 */
	if (IS_ENABLED(CONFIG_IO_EXPANDER_NCT38XX))
		if (!rv && (alert & TCPC_REG_ALERT_VENDOR_DEF))
			nct38xx_ioex_event_handler(port);

}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
static void nct38xx_drp_toggle_connection(int port,
	enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	int rv;
	int role;

	/* Get the ROLE CONTROL value */
	rv = tcpc_read(port, TCPC_REG_ROLE_CTRL, &role);
	if (rv) {
		CPRINTS("C%d: %s failed to read ROLE",
			port, __func__);
		return;
	}

	if (role & TCPC_REG_ROLE_CTRL_DRP_MASK) {
		/* TODO(b/149593609) get an understanding from Nuvoton
		 * why this is the way it works.
		 *
		 * If DRP is set, the CC pins shall stay in
		 * Potential_Connect_as_Src or Potential_Connect_as_Sink
		 * until directed otherwise.
		 *
		 * From TCPCIr2 figure 4-20 DRP Connection Detection
		 *     Determine CC & VCONN:
		 *         Set RC.CC1 & RC.CC2 per potential connect decision
		 *         Set RC.DRP=0
		 *         Set TCPC_CONTROl.PlugOrientation
		 *         Set PC.AutoDischargeDisconnect=1 & PC.EnableVconn
		 */
		int ctrl;
		enum tcpc_cc_polarity polarity;
		enum tcpc_cc_voltage_status cc1_pull, cc2_pull;
		enum tcpc_rp_value rp = TYPEC_RP_USB;

		switch (cc1) {
		case TYPEC_CC_VOLT_OPEN:
			cc1_pull = TYPEC_CC_OPEN;
			break;
		case TYPEC_CC_VOLT_RA:
			cc1_pull = TYPEC_CC_RA;
			break;
		case TYPEC_CC_VOLT_RD:
			cc1_pull = TYPEC_CC_RD;
			break;
		case TYPEC_CC_VOLT_RP_DEF:
		case TYPEC_CC_VOLT_RP_1_5:
		case TYPEC_CC_VOLT_RP_3_0:
			rp = cc1 - TYPEC_CC_VOLT_RP_DEF;
			cc1_pull = TYPEC_CC_RP;
			break;
		default:
			CPRINTS("C%d: %s Invalid CC1 Voltage presented",
				port, __func__);
			return;
		}

		switch (cc2) {
		case TYPEC_CC_VOLT_OPEN:
			cc2_pull = TYPEC_CC_OPEN;
			break;
		case TYPEC_CC_VOLT_RA:
			cc2_pull = TYPEC_CC_RA;
			break;
		case TYPEC_CC_VOLT_RD:
			cc2_pull = TYPEC_CC_RD;
			break;
		case TYPEC_CC_VOLT_RP_DEF:
		case TYPEC_CC_VOLT_RP_1_5:
		case TYPEC_CC_VOLT_RP_3_0:
			rp = cc2 - TYPEC_CC_VOLT_RP_DEF;
			cc2_pull = TYPEC_CC_RP;
			break;
		default:
			CPRINTS("C%d: %s Invalid CC2 Voltage presented",
				port, __func__);
			return;
		}

		/* Set the CC lines */
		rv = tcpc_write(port, TCPC_REG_ROLE_CTRL,
				TCPC_REG_ROLE_CTRL_SET(0,
						rp, cc1_pull, cc2_pull));
		if (rv) {
			CPRINTS("C%d: %s failed to write ROLE",
				port, __func__);
			return;
		}

		/* Set the polarity */
		if (cc_is_rp(cc1) || cc_is_rp(cc2))
			polarity = get_snk_polarity(cc1, cc2);
		else
			polarity = get_src_polarity(cc1, cc2);
		nct38xx_tcpm_drv.set_polarity(port, polarity);

		/* Set/Clear auto discharge disconnect */
		rv = tcpc_read(port, TCPC_REG_POWER_CTRL, &ctrl);
		if (rv) {
			CPRINTS("C%d: %s failed to read POWER_CTRL",
				port, __func__);
			return;
		}

		if (TCPC_REG_POWER_CTRL_VCONN(ctrl))
			ctrl |= TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
		else
			ctrl &= ~TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;

		rv = tcpc_write(port,
				TCPC_REG_POWER_CTRL,
				ctrl);
		if (rv) {
			CPRINTS("C%d: %s failed to write POWER_CTRL",
				port, __func__);
			return;
		}
	} else {
		/*
		 * We left auto-toggle and no longer have DRP set. This
		 * would happen if DRP was turned off and we did not have
		 * a connection.  We have to manually turn off that we
		 * are looking for a connection.
		 */
		rv = tcpc_update8(port,
				  TCPC_REG_TCPC_CTRL,
				  TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT,
				  MASK_CLR);
		if (rv) {
			CPRINTS("C%d: %s failed to clear Look4Connection",
				port, __func__);
			return;
		}
	}
}
#endif

const struct tcpm_drv nct38xx_tcpm_drv = {
	.init			= &nct38xx_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &tcpci_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &nct38xx_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
				  &tcpci_tcpc_enable_auto_discharge_disconnect,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
	.drp_toggle_connection	= &nct38xx_drp_toggle_connection,
#endif
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP
	.set_frs_enable         = &tcpci_tcpc_fast_role_swap_enable,
#endif
};
