/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Richtek RT1715 Type-C port controller */

#include "common.h"
#include "rt1715.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#ifndef CONFIG_USB_PD_TCPM_TCPCI
#error "RT1715 is using a standard TCPCI interface"
#error "Please upgrade your board configuration"
#endif

static int rt1715_polarity[CONFIG_USB_PD_PORT_MAX_COUNT];
static bool rt1715_initialized[CONFIG_USB_PD_PORT_MAX_COUNT];

static int rt1715_enable_ext_messages(int port, int enable)
{
	return tcpc_update8(port, RT1715_REG_VENDOR_5,
			    RT1715_REG_VENDOR_5_ENEXTMSG,
			    enable ? MASK_SET : MASK_CLR);
}

static int rt1715_tcpci_tcpm_init(int port)
{
	int rv;
	/*
	 * Do not fully reinitialize the registers when leaving low-power mode.
	 * TODO(b/179234089): Generalize this concept in the tcpm_drv API.
	 */

	/* Only do soft-reset on first init. */
	if (!(rt1715_initialized[port])) {
		/* RT1715 has a vendor-defined register reset */
		rv = tcpc_update8(port, RT1715_REG_VENDOR_7,
				  RT1715_REG_VENDOR_7_SOFT_RESET, MASK_SET);
		if (rv)
			return rv;
		rt1715_initialized[port] = true;
		crec_msleep(10);
	}

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

	/* Unmask interrupt for LPM wakeup */
	rv = tcpc_write(port, RT1715_REG_RT_MASK, RT1715_REG_RT_MASK_M_WAKEUP);
	if (rv)
		return rv;

	/*
	 * Set tTCPCFilter (CC debounce time) to 400 us
	 * (min 250 us, max 500 us).
	 */
	rv = tcpc_write(port, RT1715_REG_TTCPC_FILTER,
			RT1715_REG_TTCPC_FILTER_400US);
	if (rv)
		return rv;

	rv = tcpc_write(port, RT1715_REG_DRP_TOGGLE_CYCLE,
			RT1715_REG_DRP_TOGGLE_CYCLE_76MS);
	if (rv)
		return rv;

	/* PHY control */
	/* Set PHY control registers to Richtek recommended values */
	rv = tcpc_write(port, RT1715_REG_PHY_CTRL1,
			(RT1715_REG_PHY_CTRL1_ENRETRY |
			 RT1715_REG_PHY_CTRL1_TRANSCNT_7 |
			 RT1715_REG_PHY_CTRL1_TRXFILTER_125NS));
	if (rv)
		return rv;

	/* Set PHY control registers to Richtek recommended values */
	rv = tcpc_write(port, RT1715_REG_PHY_CTRL2,
			RT1715_REG_PHY_CTRL2_CDRTHRESH_2_58US);
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
		en = RT1715_REG_BMCIO_RXDZEN_DISABLE;

		sel = RT1715_REG_BMCIO_RXDZSEL_OCCTRL_600MA |
		      RT1715_REG_BMCIO_RXDZSEL_SEL;
	} else {
		/* RD threshold : 0.35V & RP threshold : 0.75V */
		en = RT1715_REG_BMCIO_RXDZEN_ENABLE;

		sel = RT1715_REG_BMCIO_RXDZSEL_OCCTRL_600MA |
		      RT1715_REG_BMCIO_RXDZSEL_SEL;
	}

	rv = tcpc_write(port, RT1715_REG_BMCIO_RXDZEN, en);
	if (!rv)
		rv = tcpc_write(port, RT1715_REG_BMCIO_RXDZSEL, sel);

	return rv;
}

static int rt1715_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
			 enum tcpc_cc_voltage_status *cc2)
{
	int rv;

	rv = tcpci_tcpm_get_cc(port, cc1, cc2);
	if (rv)
		return rv;

	return rt1715_init_cc_params(port, rt1715_polarity[port] ? *cc2 : *cc1);
}

/*
 * See b/179256608#comment26 for explanation.
 * Disable 24MHz oscillator and enable LPM. Upon exit from LPM, the LPEN will be
 * reset to 0.
 *
 * The exit condition for LPM is CC status change, and the wakeup interrupt will
 * be set.
 */
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int rt1715_enter_low_power_mode(int port)
{
	int regval;
	int rv;

	rv = tcpc_read(port, RT1715_REG_PWR, &regval);
	if (rv)
		return rv;

	regval |= RT1715_REG_PWR_BMCIO_LPEN;
	regval &= ~RT1715_REG_PWR_BMCIO_OSCEN;
	rv = tcpc_write(port, RT1715_REG_PWR, regval);
	if (rv)
		return rv;

	return tcpci_enter_low_power_mode(port);
}
#endif

static int rt1715_set_vconn(int port, int enable)
{
	int rv;
	int regval;

	/*
	 * Auto-idle cannot be used while sourcing Vconn.
	 * See b/179256608#comment26 for explanation.
	 */
	rv = tcpc_read(port, RT1715_REG_VENDOR_5, &regval);
	if (rv)
		return rv;

	if (enable)
		regval &= ~RT1715_REG_VENDOR_5_AUTOIDLE_EN;
	else
		regval |= RT1715_REG_VENDOR_5_AUTOIDLE_EN;

	rv = tcpc_write(port, RT1715_REG_VENDOR_5, regval);
	if (rv)
		return rv;

	return tcpci_tcpm_set_vconn(port, enable);
}

static int rt1715_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	int rv;
	enum tcpc_cc_voltage_status cc1, cc2;

	rt1715_polarity[port] = polarity;

	rv = tcpci_tcpm_get_cc(port, &cc1, &cc2);
	if (rv)
		return rv;

	rv = rt1715_init_cc_params(port, polarity ? cc2 : cc1);
	if (rv)
		return rv;

	return tcpci_tcpm_set_polarity(port, polarity);
}

static void rt1715_alert(int port)
{
	/*
	 * Make sure the wakeup interrupt is cleared. This bit is set on wakeup
	 * from LPM. See b/179256608#comment16 for explanation.
	 */
	tcpc_write(port, RT1715_REG_RT_INT, RT1715_REG_RT_INT_WAKEUP);

	tcpci_tcpc_alert(port);
}

#ifdef CONFIG_CMD_TCPC_DUMP
static const struct tcpc_reg_dump_map rt1715_regs[] = {
	{
		.addr = RT1715_REG_RT_INT,
		.name = "RT_INT",
		.size = 1,
	},
	{
		.addr = RT1715_REG_RT_MASK,
		.name = "RT_MASK",
		.size = 1,
	},
};

static void rt1715_dump_registers(int port)
{
	tcpc_dump_std_registers(port);
	tcpc_dump_registers(port, rt1715_regs, ARRAY_SIZE(rt1715_regs));
}
#endif /* defined(CONFIG_CMD_TCPC_DUMP) */

const struct tcpm_drv rt1715_tcpm_drv = {
	.init = &rt1715_tcpci_tcpm_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &rt1715_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &tcpci_tcpm_set_cc,
	.set_polarity = &rt1715_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &rt1715_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &rt1715_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
		&tcpci_tcpc_enable_auto_discharge_disconnect,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info = &tcpci_get_chip_info,
	.set_snk_ctrl = &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &rt1715_enter_low_power_mode,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
	.get_bist_test_mode = &tcpci_get_bist_test_mode,
#ifdef CONFIG_CMD_TCPC_DUMP
	.dump_registers = &rt1715_dump_registers,
#endif
};
