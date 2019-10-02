/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Silergy SYV682x USB-C Power Path Controller */
#include "common.h"
#include "console.h"
#include "driver/ppc/syv682x.h"
#include "i2c.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define SYV682X_FLAGS_SOURCE_ENABLED BIT(0)
/* 0 -> CC1, 1 -> CC2 */
#define SYV682X_FLAGS_CC_POLARITY BIT(1)
#define SYV682X_FLAGS_VBUS_PRESENT BIT(2)
static uint8_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

#define SYV682X_VBUS_DET_THRESH_MV 4000

static int read_reg(uint8_t port, int reg, int *regval)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr_flags,
			 reg,
			 regval);
}

static int write_reg(uint8_t port, int reg, int regval)
{
	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr_flags,
			  reg,
			  regval);
}

static int syv682x_is_sourcing_vbus(int port)
{
	return flags[port] & SYV682X_FLAGS_SOURCE_ENABLED;
}

static int syv682x_vbus_sink_enable(int port, int enable)
{
	int regval;
	int rv;

	/*
	 * For sink mode need to make sure high voltage power path is connected
	 * and sink mode is selected.
	 */
	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;

	if (enable) {
		/* Select high voltage path */
		regval |= SYV682X_CONTROL_1_CH_SEL;
		/* Select Sink mode and turn on the channel */
		regval &= ~(SYV682X_CONTROL_1_HV_DR |
			    SYV682X_CONTROL_1_PWR_ENB);
	} else {
		/*
		 * No need to change the voltage path or channel direction. But,
		 * turn both paths off.
		 */
		regval |= SYV682X_CONTROL_1_PWR_ENB;
	}

	return write_reg(port, SYV682X_CONTROL_1_REG, regval);
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int syv682x_is_vbus_present(int port)
{
	int val;
	int vbus = 0;

	/*
	 * TODO (b/112661747): This PPC doesn't fully support VBUS detection.
	 * It can detect both VSafe5V and VSafe0V. This function is intended
	 * here until detecting VBUS differently per channel is supported.
	 */
	if (read_reg(port, SYV682X_STATUS_REG, &val))
		return vbus;

	/*
	 * VBUS is considered present if VSafe5V is detected or neither VSafe5V
	 * or VSafe0V is detected, which implies VBUS > 5V.
	 */
	if ((val & SYV682X_STATUS_VSAFE_5V) ||
	    !(val & (SYV682X_STATUS_VSAFE_5V | SYV682X_STATUS_VSAFE_0V)))
		vbus = 1;
#ifdef CONFIG_USB_CHARGER
	if (!!(flags[port] & SYV682X_FLAGS_VBUS_PRESENT) != vbus)
		usb_charger_vbus_change(port, vbus);

	if (vbus)
		flags[port] |= SYV682X_FLAGS_VBUS_PRESENT;
	else
		flags[port] &= ~SYV682X_FLAGS_VBUS_PRESENT;
#endif

	return vbus;
}
#endif

static int syv682x_vbus_source_enable(int port, int enable)
{
	int regval;
	int rv;

	/*
	 * For source mode need to make sure 5V power path is connected
	 * and source mode is selected.
	 */
	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;

	if (enable) {
		/* Select 5V path and turn on channel */
		regval &= ~(SYV682X_CONTROL_1_CH_SEL |
			    SYV682X_CONTROL_1_PWR_ENB);
		/* Disable HV Sink path */
		regval |= SYV682X_CONTROL_1_HV_DR;
	} else if (flags[port] & SYV682X_FLAGS_SOURCE_ENABLED) {
		/*
		 * For the disable case, make sure that VBUS was being sourced
		 * prior to disabling the source path. Because the source/sink
		 * paths can't be independently disabled, and this function will
		 * get called as part of USB PD initialization, setting the
		 * PWR_ENB always can lead to broken dead battery behavior.
		 *
		 * No need to change the voltage path or channel direction. But,
		 * turn both paths off.
		 */
		regval |= SYV682X_CONTROL_1_PWR_ENB;
	}

	rv = write_reg(port, SYV682X_CONTROL_1_REG, regval);
	if (rv)
		return rv;

	if (enable)
		flags[port] |= SYV682X_FLAGS_SOURCE_ENABLED;
	else
		flags[port] &= ~SYV682X_FLAGS_SOURCE_ENABLED;

#if defined(CONFIG_USB_CHARGER) && defined(CONFIG_USB_PD_VBUS_DETECT_PPC)
	/*
	 * Since the VBUS state could be changing here, need to wake the
	 * USB_CHG_N task so that BC 1.2 detection will be triggered.
	 */
	usb_charger_vbus_change(port, enable);
#endif

	return EC_SUCCESS;
}

static int syv682x_set_vbus_source_current_limit(int port,
						 enum tcpc_rp_value rp)
{
	int rv;
	int limit;
	int regval;

	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;

	/* We need buffer room for all current values. */
	switch (rp) {
	case TYPEC_RP_3A0:
		limit = SYV682X_ILIM_3_30;
		break;

	case TYPEC_RP_1A5:
		limit = SYV682X_ILIM_1_75;
		break;

	case TYPEC_RP_USB:
	default:
		/* 1.25 A is lowest current limit setting for SVY682 */
		limit = SYV682X_ILIM_1_25;
		break;
	};

	regval &= ~SYV682X_ILIM_MASK;
	regval |= (limit << SYV682X_ILIM_BIT_SHIFT);
	return write_reg(port, SYV682X_CONTROL_1_REG, regval);
}

static int syv682x_discharge_vbus(int port, int enable)
{
	int regval;
	int rv;

	rv = read_reg(port, SYV682X_CONTROL_2_REG, &regval);
	if (rv)
		return rv;

	if (enable)
		regval |= SYV682X_CONTROL_2_FDSG;
	else
		regval &= ~SYV682X_CONTROL_2_FDSG;

	return write_reg(port, SYV682X_CONTROL_2_REG, regval);
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int syv682x_set_polarity(int port, int polarity)
{
	/*
	 * The SYV682x does not explicitly set CC polarity. However, if VCONN is
	 * being used then the polarity is required to connect 5V to the correct
	 * CC line. So this function saves the CC polarity as a bit in the flags
	 * variable so VCONN is connected the correct CC line. The flag bit
	 * being set means polarity = CC2, the flag bit clear means
	 * polarity = CC1.
	 */
	if (polarity)
		flags[port] |= SYV682X_FLAGS_CC_POLARITY;
	else
		flags[port] &= ~SYV682X_FLAGS_CC_POLARITY;

	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_USBC_PPC_VCONN
static int syv682x_set_vconn(int port, int enable)
{
	int regval;
	int rv;

	rv = read_reg(port, SYV682X_CONTROL_4_REG, &regval);
	if (rv)
		return rv;

	if (enable)
		regval |= flags[port] & SYV682X_FLAGS_CC_POLARITY ?
			SYV682X_CONTROL_4_VCONN1 : SYV682X_CONTROL_4_VCONN2;
	else
		regval &= ~(SYV682X_CONTROL_4_VCONN2 |
			    SYV682X_CONTROL_4_VCONN1);

	return write_reg(port, SYV682X_CONTROL_4_REG, regval);
}
#endif

#ifdef CONFIG_CMD_PPC_DUMP
static int syv682x_dump(int port)
{
	int reg_addr;
	int data;
	int rv;
	const int i2c_port = ppc_chips[port].i2c_port;
	const int i2c_addr_flags = ppc_chips[port].i2c_addr_flags;

	for (reg_addr = SYV682X_STATUS_REG; reg_addr <= SYV682X_CONTROL_4_REG;
	     reg_addr++) {
		rv = i2c_read8(i2c_port, i2c_addr_flags, reg_addr, &data);
		if (rv)
			ccprintf("ppc_syv682[p%d]: Failed to read reg 0x%02x\n",
				 port, reg_addr);
		else
			ccprintf("ppc_syv682[p%d]: reg 0x%02x = 0x%02x\n",
				 port, reg_addr, data);
	}

	cflush();

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

static int syv682x_init(int port)
{
	int rv;
	int regval;

	/* Set VBUS discharge to manual mode */
	rv = read_reg(port, SYV682X_CONTROL_2_REG, &regval);
	if (rv)
		return rv;
	regval &= ~SYV682X_CONTROL_2_SDSG;
	rv = write_reg(port, SYV682X_CONTROL_2_REG, regval);
	if (rv)
		return rv;

	/* Select max voltage for OVP */
	rv = read_reg(port, SYV682X_CONTROL_3_REG, &regval);
	if (rv)
		return rv;
	regval &= ~SYV682X_OVP_MASK;
	regval |= (SYV682X_OVP_23_7 << SYV682X_OVP_BIT_SHIFT);
	rv = write_reg(port, SYV682X_CONTROL_3_REG, regval);
	if (rv)
		return rv;

	/* Check if this if dead battery case */
	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;
	if (regval & SYV682X_STATUS_VSAFE_0V) {
		/* Not dead battery case, so disable channel */
		regval |= SYV682X_CONTROL_1_PWR_ENB;
		rv = write_reg(port, SYV682X_CONTROL_1_REG, regval);
		if (rv)
			return rv;
	} else {
		syv682x_vbus_sink_enable(port, 1);
	}

	rv = read_reg(port, SYV682X_CONTROL_4_REG, &regval);
	if (rv)
		return rv;
	/* Remove Rd and connect CC1/CC2 lines to TCPC */
	regval |= SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS;
	/* Disable Fast Role Swap (FRS) */
	regval |= SYV682X_CONTROL_4_CC_FRS;
	rv = write_reg(port, SYV682X_CONTROL_4_REG, regval);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

const struct ppc_drv syv682x_drv = {
	.init = &syv682x_init,
	.is_sourcing_vbus = &syv682x_is_sourcing_vbus,
	.vbus_sink_enable = &syv682x_vbus_sink_enable,
	.vbus_source_enable = &syv682x_vbus_source_enable,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &syv682x_dump,
#endif /* defined(CONFIG_CMD_PPC_DUMP) */
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &syv682x_is_vbus_present,
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */
	.set_vbus_source_current_limit = &syv682x_set_vbus_source_current_limit,
	.discharge_vbus = &syv682x_discharge_vbus,
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &syv682x_set_polarity,
#endif
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &syv682x_set_vconn,
#endif
};
