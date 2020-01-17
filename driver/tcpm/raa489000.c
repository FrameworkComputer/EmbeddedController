/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas RAA489000 TCPC driver
 */

#include "common.h"
#include "console.h"
#include "driver/charger/isl923x.h"
#include "i2c.h"
#include "raa489000.h"
#include "tcpci.h"
#include "tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int raa489000_init(int port)
{
	int rv;
	int regval;
	int i2c_port;

	/* Perform unlock sequence */
	rv = tcpc_write16(port, 0xAA, 0xDAA0);
	if (rv)
		CPRINTS("c%d: failed unlock step1", port);
	rv = tcpc_write16(port, 0xAA, 0xACE0);
	if (rv)
		CPRINTS("c%d: failed unlock step2", port);
	rv = tcpc_write16(port, 0xAA, 0x0D0B);
	if (rv)
		CPRINTS("c%d: failed unlock step3", port);

	/* Note: registers may not be ready until TCPCI init succeeds */
	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

	/*
	 * Set some vendor defined registers to enable the CC comparators and
	 * remove the dead battery resistors.
	 */
	rv = tcpc_write16(port, RAA489000_TYPEC_SETTING1,
		     RAA489000_SETTING1_RDOE | RAA489000_SETTING1_CC2_CMP3_EN |
		     RAA489000_SETTING1_CC2_CMP2_EN |
		     RAA489000_SETTING1_CC2_CMP1_EN |
		     RAA489000_SETTING1_CC1_CMP3_EN |
		     RAA489000_SETTING1_CC1_CMP2_EN |
		     RAA489000_SETTING1_CC1_CMP1_EN |
		     RAA489000_SETTING1_CC_DB_EN);
	if (rv)
		CPRINTS("c%d: failed to enable CC comparators", port);

	/* Enable the ADC */
	/*
	 * TODO(b:147316511) Since this register can be accessed by multiple
	 * tasks, we should add a mutex when modifying this register.
	 */
	i2c_port = tcpc_config[port].i2c_info.port;
	rv = i2c_read16(i2c_port, ISL923X_ADDR_FLAGS, ISL9238_REG_CONTROL3,
			&regval);
	regval |= RAA489000_ENABLE_ADC;
	rv |= i2c_write16(i2c_port, ISL923X_ADDR_FLAGS, ISL9238_REG_CONTROL3,
			  regval);
	if (rv)
		CPRINTS("c%d: failed to enable ADCs", port);

	/* Enable Vbus detection */
	rv = tcpc_write(port, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_ENABLE_VBUS_DETECT);
	if (rv)
		CPRINTS("c%d: failed to enable vbus detect cmd", port);


	/* Set Rx enable for receiver comparator */
	rv = tcpc_read16(port, RAA489000_PD_PHYSICAL_SETTING1, &regval);
	regval |= RAA489000_PD_PHY_SETTING1_RECEIVER_EN |
		RAA489000_PD_PHY_SETTING1_SQUELCH_EN |
		RAA489000_PD_PHY_SETTING1_TX_LDO11_EN;
	rv |= tcpc_write16(port, RAA489000_PD_PHYSICAL_SETTING1, regval);
	if (rv)
		CPRINTS("c%d: failed to set PD PHY setting1", port);

	/* Enable VBUS auto discharge. needed to goodcrc */
	rv = tcpc_read(port, TCPC_REG_POWER_CTRL, &regval);
	regval |= TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
	rv |= tcpc_write(port, TCPC_REG_POWER_CTRL, regval);
	if (rv)
		CPRINTS("c%d: failed to set auto discharge", port);

	/* The vendor says to set this setting. */
	rv = tcpc_write16(port, RAA489000_PD_PHYSICAL_PARAMETER1, 0x6C07);
	if (rv)
		CPRINTS("c%d: failed to set PD PHY PARAM1", port);

	/* Enable the correct TCPCI interface version */
	rv = tcpc_read16(port, RAA489000_TCPC_SETTING1, &regval);
	if (!(tcpc_config[port].flags & TCPC_FLAGS_TCPCI_V2_0))
		regval |= RAA489000_TCPCV1_0_EN;
	else
		regval &= ~RAA489000_TCPCV1_0_EN;
	rv = tcpc_write16(port, RAA489000_TCPC_SETTING1, regval);
	if (rv)
		CPRINTS("c%d: failed to set TCPCIv1.0 mode", port);

	return rv;
}

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
	.init                   = &raa489000_init,
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
