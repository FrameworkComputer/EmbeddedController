/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MT6370 TCPC Driver
 */

#include "console.h"
#include "hooks.h"
#include "mt6370.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static int mt6370_polarity;

/* i2c_write function which won't wake TCPC from low power mode. */
static int mt6370_i2c_write8(int port, int reg, int val)
{
	return i2c_write8(tcpc_config[port].i2c_info.port,
			  tcpc_config[port].i2c_info.addr_flags, reg, val);
}

static int mt6370_init(int port)
{
	int rv, val;

	rv = tcpc_read(port, MT6370_REG_IDLE_CTRL, &val);

	/* Only do soft-reset in shipping mode. (b:122017882) */
	if (!(val & MT6370_REG_SHIPPING_OFF)) {
		/* Software reset. */
		rv = tcpc_write(port, MT6370_REG_SWRESET, 1);
		if (rv)
			return rv;

		/* Need 1 ms for software reset. */
		crec_msleep(1);
	}

	/* The earliest point that we can do generic init. */
	rv = tcpci_tcpm_init(port);

	if (rv)
		return rv;

	/*
	 * AUTO IDLE off, shipping off, select CK_300K from BICIO_320K,
	 * PD3.0 ext-msg on.
	 */
	rv = tcpc_write(port, MT6370_REG_IDLE_CTRL,
			MT6370_REG_IDLE_SET(0, 1, 0, 0));
	/* CC Detect Debounce 5 */
	rv |= tcpc_write(port, MT6370_REG_TTCPC_FILTER, 5);
	/* DRP Duty */
	rv |= tcpc_write(port, MT6370_REG_DRP_TOGGLE_CYCLE, 4);
	rv |= tcpc_write16(port, MT6370_REG_DRP_DUTY_CTRL, 400);
	/* Vconn OC on */
	rv |= tcpc_write(port, MT6370_REG_VCONN_CLIMITEN, 1);
	/* PHY control */
	rv |= tcpc_write(port, MT6370_REG_PHY_CTRL1,
			 MT6370_REG_PHY_CTRL1_SET(0, 7, 0, 1));
	rv |= tcpc_write(port, MT6370_REG_PHY_CTRL3, 0x82);

	return rv;
}

static inline int mt6370_init_cc_params(int port, int cc_res)
{
	int rv, en, sel;

	if (cc_res == TYPEC_CC_VOLT_RP_DEF) { /* RXCC threshold : 0.55V */
		en = 1;
		sel = MT6370_OCCTRL_600MA | MT6370_MASK_BMCIO_RXDZSEL;
	} else { /* RD threshold : 0.4V & RP threshold : 0.7V */
		en = 0;
		sel = MT6370_OCCTRL_600MA;
	}
	rv = tcpc_write(port, MT6370_REG_BMCIO_RXDZEN, en);
	if (!rv)
		rv = tcpc_write(port, MT6370_REG_BMCIO_RXDZSEL, sel);
	return rv;
}

static int mt6370_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
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
	 * MT6370 TCPC follows USB PD 1.0 protocol. When DRP not auto-toggling,
	 * it will not update the DRP_RESULT bits in TCPC_REG_CC_STATUS,
	 * instead, we should check CC1/CC2 bits in TCPC_REG_ROLE_CTRL.
	 */
	rv = tcpc_read(port, TCPC_REG_ROLE_CTRL, &role);

	if (TCPC_REG_ROLE_CTRL_DRP(role))
		is_snk = TCPC_REG_CC_STATUS_TERM(status);
	else
		/* CC1/CC2 states are the same, checking one-side is enough. */
		is_snk = TCPC_REG_CC_STATUS_CC1(role) == TYPEC_CC_RD;

	if (is_snk) {
		if (*cc1 != TYPEC_CC_VOLT_OPEN)
			*cc1 |= 0x04;
		if (*cc2 != TYPEC_CC_VOLT_OPEN)
			*cc2 |= 0x04;
	}

	rv = mt6370_init_cc_params(port, (int)mt6370_polarity ? *cc1 : *cc2);
	return rv;
}

static int mt6370_set_cc(int port, int pull)
{
	if (pull == TYPEC_CC_RD)
		mt6370_init_cc_params(port, TYPEC_CC_VOLT_RP_DEF);
	return tcpci_tcpm_set_cc(port, pull);
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int mt6370_enter_low_power_mode(int port)
{
	int rv;

	/* VBUS_DET_EN for detecting charger plug. */
	rv = tcpc_write(port, MT6370_REG_BMC_CTRL,
			MT6370_REG_BMCIO_LPEN | MT6370_REG_VBUS_DET_EN);

	if (rv)
		return rv;

	return tcpci_enter_low_power_mode(port);
}
#endif

static int mt6370_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	mt6370_polarity = polarity;
	mt6370_get_cc(port, &cc1, &cc2);
	return tcpci_tcpm_set_polarity(port, polarity);
}

int mt6370_vconn_discharge(int port)
{
	/*
	 * Write to mt6370 in low-power mode may return fail, but it is
	 * actually written. So we just ignore its return value.
	 */
	mt6370_i2c_write8(port, MT6370_REG_OVP_FLAG_SEL,
			  MT6370_REG_DISCHARGE_LVL);
	/* Set MT6370_REG_DISCHARGE_EN bit and also the rest default value. */
	mt6370_i2c_write8(port, MT6370_REG_BMC_CTRL,
			  MT6370_REG_DISCHARGE_EN |
				  MT6370_REG_BMC_CTRL_DEFAULT);

	return EC_SUCCESS;
}

/* MT6370 is a TCPCI compatible port controller */
const struct tcpm_drv mt6370_tcpm_drv = {
	.init = &mt6370_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &mt6370_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &mt6370_set_cc,
	.set_polarity = &mt6370_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
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
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info = &tcpci_get_chip_info,
	.set_snk_ctrl = &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &mt6370_enter_low_power_mode,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
	.get_bist_test_mode = &tcpci_get_bist_test_mode,
};
