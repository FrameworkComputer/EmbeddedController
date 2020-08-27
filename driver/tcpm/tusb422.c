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

#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) && \
	defined(CONFIG_USB_PD_DISCHARGE_TCPC)
#error "TUSB422 must disable TCPC discharge to support enabling Auto Discharge"
#error "Disconnect all the time."
#endif

enum tusb422_reg_addr {
	TUSB422_REG_VBUS_AND_VCONN_CONTROL = 0x98,
};

enum vbus_and_vconn_control_mask {
	INT_VCONNDIS_DISABLE = BIT(1),
	INT_VBUSDIS_DISABLE  = BIT(2),
};

/* The TUSB422 cannot drive an FRS GPIO, but can detect FRS */
static int tusb422_set_frs_enable(int port, int enable)
{
	return tcpc_update8(port, TUSB422_REG_PHY_BMC_RX_CTRL,
			    TUSB422_REG_PHY_BMC_RX_CTRL_FRS_RX_EN,
			    enable ? MASK_SET : MASK_CLR);
}

static int tusb422_tcpci_tcpm_init(int port)
{
	int rv;

	/* TUSB422 has a vendor-defined register reset */
	rv = tcpc_update8(port, TUSB422_REG_CC_GEN_CTRL,
			  TUSB422_REG_CC_GEN_CTRL_GLOBAL_SW_RST, MASK_SET);
	if (rv)
		return rv;

	rv = tcpci_tcpm_init(port);
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

		/*
		 * Disable internal VBUS discharge. AutoDischargeDisconnect must
		 * generally remain enabled to keep TUSB422 in active mode.
		 * However, this will interfere with FRS by default by
		 * discharging at inappropriate times. Mitigate this by
		 * disabling internal VBUS discharge. The TUSB422 must rely on
		 * external VBUS discharge. See TUSB422 datasheet, 7.4.2 Active
		 * Mode.
		 */
		tcpc_write(port, TUSB422_REG_VBUS_AND_VCONN_CONTROL,
				INT_VBUSDIS_DISABLE);
	}
	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC)) {
		/* Disable FRS detection, and enable the FRS detection alert */
		tusb422_set_frs_enable(port, 0);
		tcpc_update16(port, TCPC_REG_ALERT_MASK,
			      TCPC_REG_ALERT_MASK_VENDOR_DEF, MASK_SET);
		tcpc_update8(port, TUSB422_REG_VENDOR_INTERRUPTS_MASK,
			     TUSB422_REG_VENDOR_INTERRUPTS_MASK_FRS_RX,
			     MASK_SET);
	}
	/*
	 * VBUS detection is supposed to be enabled by default, however the
	 * TUSB422 has this disabled following reset.
	 */
	/* Enable VBUS detection */
	return tcpc_write16(port, TCPC_REG_COMMAND,
				TCPC_REG_COMMAND_ENABLE_VBUS_DETECT);
}

static int tusb422_tcpm_set_cc(int port, int pull)
{
	/*
	 * Enable AutoDischargeDisconnect to keep TUSB422 in active mode through
	 * this transition. Note that the configuration keeps the TCPC from
	 * actually discharging VBUS in this case.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE))
		tusb422_tcpm_drv.tcpc_enable_auto_discharge_disconnect(port, 1);

	return tcpci_tcpm_set_cc(port, pull);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
static int tusb422_tcpc_drp_toggle(int port)
{
	/*
	 * The TUSB422 requires auto discharge disconnect to be enabled for
	 * active mode (not unattached) operation. Make sure it is disabled
	 * before enabling DRP toggling.
	 *
	 * USB Type-C Port Controller Interface Specification revision 2.0,
	 * Figure 4-21 Source Disconnect and Figure 4-22 Sink Disconnect
	 */
	tusb422_tcpm_drv.tcpc_enable_auto_discharge_disconnect(port, 0);

	return tcpci_tcpc_drp_toggle(port);
}
#endif

static void tusb422_tcpci_tcpc_alert(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC)) {
		int regval;

		/* FRS detection is a vendor defined alert */
		tcpc_read(port, TUSB422_REG_VENDOR_INTERRUPTS_STATUS, &regval);
		if (regval & TUSB422_REG_VENDOR_INTERRUPTS_STATUS_FRS_RX) {
			tusb422_set_frs_enable(port, 0);
			tcpc_write(port, TUSB422_REG_VENDOR_INTERRUPTS_STATUS,
				   regval);
			pd_got_frs_signal(port);
		}
	}
	tcpci_tcpc_alert(port);
}

const struct tcpm_drv tusb422_tcpm_drv = {
	.init			= &tusb422_tcpci_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tusb422_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_disable	= &tcpci_tcpm_sop_prime_disable,
#endif
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tusb422_tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
				  &tcpci_tcpc_enable_auto_discharge_disconnect,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tusb422_tcpc_drp_toggle,
#endif
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable         = &tusb422_set_frs_enable,
#endif
};
