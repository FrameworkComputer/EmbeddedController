/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager for Nuvoton NCT38XX. */

#include "common.h"
#include "console.h"
#include "hooks.h"
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

static int nct38xx_init(int port)
{
	int rv;
	int reg;

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
	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC)) {
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
	if (rv)
		return rv;

	/**
	 * Set driver specific ALERT mask bits
	 *
	 * Wake up on faults
	 */
	reg = TCPC_REG_ALERT_FAULT;

	/*
	 * Enable the Vendor Define alert event only when the IO expander
	 * feature is defined
	 */
	if (IS_ENABLED(CONFIG_IO_EXPANDER_NCT38XX))
		reg |= TCPC_REG_ALERT_VENDOR_DEF;

	rv = tcpc_update16(port,
			   TCPC_REG_ALERT_MASK,
			   reg,
			   MASK_SET);

	return rv;
}

static int nct38xx_tcpm_init(int port)
{
	int rv;

	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

	return nct38xx_init(port);
}

static int nct38xx_tcpm_set_cc(int port, int pull)
{
	/*
	 * Setting the CC lines to open/open requires that the NCT CTRL_OUT
	 * register has sink disabled. Otherwise, when no battery is connected:
	 *
	 * 1. You set CC lines to Open/Open. This is physically happening on
	 *    the CC line.
	 * 2. Since CC is now Open/Open, the internal TCPC HW state machine
	 *    is no longer in Attached.Snk and therefore our TCPC HW
	 *    automatically opens the sink switch (de-assert the VBSNK_EN pin)
	 * 3. Since sink switch is open, the TCPC VCC voltage starts to drop.
	 * 4. When TCPC VCC gets below ~2.7V the TCPC will reset and therefore
	 *    it will present Rd/Rd on the CC lines. Also the VBSNK_EN pin
	 *    after reset is Hi-Z, so the sink switch will get closed again.
	 *
	 * Disabling SNKEN makes the VBSNK_EN pin Hi-Z, so
	 * USB_Cx_TCPC_VBSNK_EN_L will be asserted by external
	 * pull-down, so only do so if already sinking, otherwise
	 * both source and sink switches can be closed, which should
	 * never happen (b/166850036).
	 *
	 * SNKEN will be re-enabled in nct38xx_init above (from tcpm_init), or
	 * when CC lines are set again, or when sinking is disabled.
	 */
	enum mask_update_action action = MASK_SET;
	int rv;

	if (pull == TYPEC_CC_OPEN) {
		bool is_sinking;

		rv = tcpm_get_snk_ctrl(port, &is_sinking);
		if (rv)
			return rv;

		if (is_sinking)
			action = MASK_CLR;
	}

	rv = tcpc_update8(port,
			  NCT38XX_REG_CTRL_OUT_EN,
			  NCT38XX_REG_CTRL_OUT_EN_SNKEN,
			  action);
	if (rv)
		return rv;

	return tcpci_tcpm_set_cc(port, pull);
}

static int nct38xx_tcpm_set_snk_ctrl(int port, int enable)
{
	int rv;

	/*
	 * To disable sinking, SNKEN must be enabled so that
	 * USB_Cx_TCPC_VBSNK_EN_L will be driven high.
	 */
	if (!enable) {
		rv = tcpc_update8(port,
				  NCT38XX_REG_CTRL_OUT_EN,
				  NCT38XX_REG_CTRL_OUT_EN_SNKEN,
				  MASK_SET);
		if (rv)
			return rv;
	}

	return tcpci_tcpm_set_snk_ctrl(port, enable);
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

static int nct3807_handle_fault(int port, int fault)
{
	int rv = EC_SUCCESS;

	/* Registers are set to default, initialize for our use */
	if (fault & TCPC_REG_FAULT_STATUS_ALL_REGS_RESET) {
		rv = nct38xx_init(port);
	} else {
		/* We don't use TCPC OVP, so just disable it */
		if (fault & TCPC_REG_FAULT_STATUS_VBUS_OVER_VOLTAGE) {
			/* Disable OVP */
			rv = tcpc_update8(port,
				  TCPC_REG_FAULT_CTRL,
				  TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS,
				  MASK_SET);
			if (rv)
				return rv;
		}
		/* Failing AutoDischargeDisconnect should disable it */
		if (fault & TCPC_REG_FAULT_STATUS_AUTO_DISCHARGE_FAIL)
			tcpm_enable_auto_discharge_disconnect(port, 0);
	}
	return rv;
}

const struct tcpm_drv nct38xx_tcpm_drv = {
	.init			= &nct38xx_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &nct38xx_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_disable	= &tcpci_tcpm_sop_prime_disable,
#endif
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
	.debug_accessory	= &tcpci_tcpc_debug_accessory,

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
#ifdef CONFIG_USBC_PPC
	.get_snk_ctrl		= &tcpci_tcpm_get_snk_ctrl,
	.set_snk_ctrl		= &nct38xx_tcpm_set_snk_ctrl,
	.get_src_ctrl		= &tcpci_tcpm_get_src_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable         = &tcpci_tcpc_fast_role_swap_enable,
#endif
	.handle_fault		= &nct3807_handle_fault,
};
