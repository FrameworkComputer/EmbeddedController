/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Richtek RT1715 Type-C port controller */

#include "common.h"
#include "rt1715.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#ifndef CONFIG_USB_PD_TCPM_TCPCI
#error "RT1715 is using a standard TCPCI interface"
#error "Please upgrade your board configuration"
#endif

static int rt1715_polarity[CONFIG_USB_PD_PORT_MAX_COUNT];

static int rt1715_enable_ext_messages(int port, int enable)
{
	return tcpc_update8(port, RT1715_REG_VENDOR_5,
		  RT1715_REG_VENDOR_5_ENEXTMSG,
		  enable ? MASK_SET : MASK_CLR);
}

static int rt1715_tcpci_tcpm_init(int port)
{
	int rv;
	/* RT1715 has a vendor-defined register reset */
	rv = tcpc_update8(port, RT1715_REG_VENDOR_7,
		  RT1715_REG_VENDOR_7_SOFT_RESET, MASK_SET);
	if (rv)
		return rv;

	msleep(10);

	rv = tcpc_update8(port, RT1715_REG_VENDOR_5,
		  RT1715_REG_VENDOR_5_SHUTDOWN_OFF, MASK_SET);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_USB_PD_REV30))
		rt1715_enable_ext_messages(port, 1);

	rv = tcpc_write(port, RT1715_REG_I2CRST_CTRL,
		  (RT1715_REG_I2CRST_CTRL_EN |
			  RT1715_REG_I2CRST_CTRL_TOUT_200MS));
	if (rv)
		return rv;

	/* tTCPCfilter : (26.7 * val) us */
	rv = tcpc_write(port, RT1715_REG_TTCPC_FILTER,
		  RT1715_REG_TTCPC_FILTER_400US);
	if (rv)
		return rv;

	/* DRP Duty : (51.2 + 6.4 * val) ms */
	rv = tcpc_write(port, RT1715_REG_DRP_TOGGLE_CYCLE,
		  RT1715_REG_DRP_TOGGLE_CYCLE_76MS);
	if (rv)
		return rv;

	/* dcSRC.DRP : 40% */
	rv = tcpc_write16(port, RT1715_REG_DRP_DUTY_CTRL,
		  RT1715_REG_DRP_DUTY_CTRL_40PERCENT);
	if (rv)
		return rv;

	/* PHY control */
	rv = tcpc_write(port, RT1715_REG_PHY_CTRL1, 0xF1);
	if (rv)
		return rv;

	rv = tcpc_write(port, RT1715_REG_PHY_CTRL2, 0x36);
	if (rv)
		return rv;

	return tcpci_tcpm_init(port);
}

/*
 * Selects the CC PHY noise filter voltage level according to the current
 * CC voltage level.
 *
 * @param cc_level The CC voltage level for the port's current role
 * @return EC_SUCCESS if writes succeed; failure code otherwise
 */
static inline int rt1715_init_cc_params(int port, int cc_level)
{
	int rv, en, sel;

	if (cc_level == TYPEC_CC_VOLT_RP_DEF) {
		/* RXCC threshold : 0.55V */
		en = 0;

		sel = RT1715_REG_BMCIO_RXDZSEL_OCCTRL_600MA
		  | RT1715_REG_BMCIO_RXDZSEL_MASK;
	} else {
		/* RD threshold : 0.35V & RP threshold : 0.75V */
		en = 1;

		sel = RT1715_REG_BMCIO_RXDZSEL_OCCTRL_600MA
				  | RT1715_REG_BMCIO_RXDZSEL_MASK;
	}

	rv = tcpc_write(port, RT1715_REG_BMCIO_RXDZEN, en);
	if (!rv)
		rv = tcpc_write(port, RT1715_REG_BMCIO_RXDZSEL, sel);

	return rv;
}

static int rt1715_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	int status;
	int rv;
	int role, is_snk;

	rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);
	/* If tcpc read fails, return error and CC as open */
	if (rv) {
		*cc1 = TYPEC_CC_VOLT_OPEN;

		*cc2 = TYPEC_CC_VOLT_OPEN;

		return rv;
	}
	*cc1 = TCPC_REG_CC_STATUS_CC1(status);

	*cc2 = TCPC_REG_CC_STATUS_CC2(status);

	/*
	 * If status is not open, then OR in termination to convert to
	 * enum tcpc_cc_voltage_status.
	 *
	 * RT1715 TCPC follows TCPCI 0.6 protocol. When DRP not auto-toggling,
	 * it will not update the DRP_RESULT bits in TCPC_REG_CC_STATUS,
	 * instead, we should check CC1/CC2 bits in TCPC_REG_ROLE_CTRL.
	 */
	rv = tcpc_read(port, TCPC_REG_ROLE_CTRL, &role);
	if (rv)
		return rv;

	if (TCPC_REG_ROLE_CTRL_DRP(role))
		is_snk = TCPC_REG_CC_STATUS_TERM(status);
	else
		/* CC1/CC2 states are the same, checking one-side is enough. */
		is_snk = TCPC_REG_ROLE_CTRL_CC1(role) == TYPEC_CC_RD;

	if (is_snk) {
		if (*cc1 != TYPEC_CC_VOLT_OPEN)
			*cc1 |= 0x04;

		if (*cc2 != TYPEC_CC_VOLT_OPEN)
			*cc2 |= 0x04;
	}
	rv = rt1715_init_cc_params(port, rt1715_polarity[port] ? *cc2 : *cc1);

	return rv;
}

static int rt1715_set_cc(int port, int pull)
{
	if (pull == TYPEC_CC_RD)
		rt1715_init_cc_params(port, TYPEC_CC_VOLT_RP_DEF);

	return tcpci_tcpm_set_cc(port, pull);
}

static int rt1715_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	rt1715_polarity[port] = polarity;

	rt1715_get_cc(port, &cc1, &cc2);

	return tcpci_tcpm_set_polarity(port, polarity);
}

const struct tcpm_drv rt1715_tcpm_drv = {
	.init = &rt1715_tcpci_tcpm_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &rt1715_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &rt1715_set_cc,
	.set_polarity = &rt1715_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable   = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &tcpci_tcpm_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
		&tcpci_tcpc_enable_auto_discharge_disconnect,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl = &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
#endif
	.get_chip_info = &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &tcpci_enter_low_power_mode,
#endif
};
