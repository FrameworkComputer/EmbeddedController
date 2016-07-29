/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ANX7688 port manager */

#include "hooks.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_mux.h"

#define ANX7688_VENDOR_ALERT    (1 << 15)

#define ANX7688_REG_STATUS      0x82
#define ANX7688_REG_STATUS_LINK (1 << 0)

#define ANX7688_REG_HPD         0x83
#define ANX7688_REG_HPD_HIGH    (1 << 0)
#define ANX7688_REG_HPD_IRQ     (1 << 1)
#define ANX7688_REG_HPD_ENABLE  (1 << 2)

#define ANX7688_USBC_ADDR		0x50
#define ANX7688_REG_RAMCTRL		0xe7
#define ANX7688_REG_RAMCTRL_BOOT_DONE	(1 << 6)

static int anx7688_init(int port)
{
	int rv = 0;
	int mask = 0;

	/*
	 * 7688 POWER_STATUS[6] is not reliable for tcpci_tcpm_init() to poll
	 * due to it is default 0 in HW, and we cannot write TCPC until it is
	 * ready, or something goes wrong. (Issue 52772)
	 * Instead we poll TCPC 0x50:0xe7 bit6 here to make sure bootdone is
	 * ready(50ms). Then PD main flow can process cc debounce in 50ms ~
	 * 100ms to follow cts.
	 */
	while (1) {
		rv = i2c_read8(I2C_PORT_TCPC, ANX7688_USBC_ADDR,
			       ANX7688_REG_RAMCTRL, &mask);

		if (rv == EC_SUCCESS && (mask & ANX7688_REG_RAMCTRL_BOOT_DONE))
			break;
		msleep(10);
	}

	rv = tcpci_tcpm_drv.init(port);
	if (rv)
		return rv;

	rv = tcpc_read16(port, TCPC_REG_ALERT_MASK, &mask);
	if (rv)
		return rv;

	/* enable vendor specific alert */
	mask |= ANX7688_VENDOR_ALERT;
	rv = tcpc_write16(port, TCPC_REG_ALERT_MASK, mask);
	return rv;
}

static void anx7688_update_hpd_enable(int port)
{
	int status, reg, rv;

	rv = tcpc_read(port, ANX7688_REG_STATUS, &status);
	rv |= tcpc_read(port, ANX7688_REG_HPD, &reg);
	if (rv)
		return;

	if (!(reg & ANX7688_REG_HPD_ENABLE) ||
	    !(status & ANX7688_REG_STATUS_LINK)) {
		reg &= ~ANX7688_REG_HPD_IRQ;
		tcpc_write(port, ANX7688_REG_HPD,
			   (status & ANX7688_REG_STATUS_LINK)
			   ? reg | ANX7688_REG_HPD_ENABLE
			   : reg & ~ANX7688_REG_HPD_ENABLE);
	}
}

int anx7688_hpd_disable(int port)
{
	return tcpc_write(port, ANX7688_REG_HPD, 0);
}

int anx7688_update_hpd(int port, int level, int irq)
{
	int reg, rv;

	rv = tcpc_read(port, ANX7688_REG_HPD, &reg);
	if (rv)
		return rv;

	if (level)
		reg |= ANX7688_REG_HPD_HIGH;
	else
		reg &= ~ANX7688_REG_HPD_HIGH;

	if (irq)
		reg |= ANX7688_REG_HPD_IRQ;
	else
		reg &= ~ANX7688_REG_HPD_IRQ;

	return tcpc_write(port, ANX7688_REG_HPD, reg);
}

int anx7688_enable_cable_detection(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, 0xff);
}

int anx7688_set_power_supply_ready(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, 0x77);
}

int anx7688_power_supply_reset(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, 0x66);
}

static void anx7688_tcpc_alert(int port)
{
	int alert, rv;

	rv = tcpc_read16(port, TCPC_REG_ALERT, &alert);
	/* process and clear alert status */
	tcpci_tcpm_drv.tcpc_alert(port);

	if (!rv && (alert & ANX7688_VENDOR_ALERT))
		anx7688_update_hpd_enable(port);
}

static int anx7688_mux_set(int i2c_addr, mux_state_t mux_state)
{
	int reg = 0;
	int rv, polarity;
	int port = i2c_addr; /* use port index in port_addr field */

	rv = tcpc_read(port, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	reg &= ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK;
	if (mux_state & MUX_USB_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	if (mux_state & MUX_DP_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP;

	/* ANX7688 needs to set bit0 */
	rv = tcpc_read(port, TCPC_REG_TCPC_CTRL, &polarity);
	if (rv != EC_SUCCESS)
		return rv;

	/* copy the polarity from TCPC_CTRL[0], take care clear then set */
	reg &= ~TCPC_REG_TCPC_CTRL_POLARITY(1);
	reg |= TCPC_REG_TCPC_CTRL_POLARITY(polarity);
	return tcpc_write(port, TCPC_REG_CONFIG_STD_OUTPUT, reg);
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
static int anx7688_tcpm_get_vbus_level(int port)
{
	int reg = 0;

	/* On ANX7688, POWER_STATUS.VBusPresent (bit 2) is averaged 16 times, so
	 * its value may not be set to 1 quickly enough during power role swap.
	 * Therefore, we use a proprietary register to read the unfiltered VBus
	 * value. See crosbug.com/p/55221 .
	 */
	i2c_read8(I2C_PORT_TCPC, 0x50, 0x40, &reg);
	return ((reg & 0x10) ? 1 : 0);
}
#endif

/* ANX7688 is a TCPCI compatible port controller */
const struct tcpm_drv anx7688_tcpm_drv = {
	.init			= &anx7688_init,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &anx7688_tcpm_get_vbus_level,
#endif
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message		= &tcpci_tcpm_get_message,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &anx7688_tcpc_alert,
};

#ifdef CONFIG_USB_PD_TCPM_MUX
const struct usb_mux_driver anx7688_usb_mux_driver = {
	.init = tcpci_tcpm_mux_init,
	.set = anx7688_mux_set,
	.get = tcpci_tcpm_mux_get,
};
#endif /* CONFIG_USB_PD_TCPM_MUX */

