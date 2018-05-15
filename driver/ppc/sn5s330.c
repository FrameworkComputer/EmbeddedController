/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI SN5S330 USB-C Power Path Controller */

/*
 * PP1 : Sourcing power path.
 * PP2 : Sinking power path.
 */

#include "common.h"
#include "console.h"
#include "driver/ppc/sn5s330.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint32_t irq_pending; /* Bitmask of ports signaling an interrupt. */

static int read_reg(uint8_t port, int reg, int *regval)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr,
			 reg,
			 regval);
}

static int write_reg(uint8_t port, int reg, int regval)
{
	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr,
			  reg,
			  regval);
}

#ifdef CONFIG_CMD_PPC_DUMP
static int sn5s330_dump(int port)
{
	int i;
	int data;
	const int i2c_port = ppc_chips[port].i2c_port;
	const int i2c_addr = ppc_chips[port].i2c_addr;

	for (i = SN5S330_FUNC_SET1; i <= SN5S330_FUNC_SET12; i++) {
		i2c_read8(i2c_port, i2c_addr, i, &data);
		ccprintf("FUNC_SET%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_FUNC_SET1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_STATUS_REG1; i <= SN5S330_INT_STATUS_REG4; i++) {
		i2c_read8(i2c_port, i2c_addr, i, &data);
		ccprintf("INT_STATUS_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_STATUS_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_TRIP_RISE_REG1; i <= SN5S330_INT_TRIP_RISE_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr, i, &data);
		ccprintf("INT_TRIP_RISE_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_TRIP_RISE_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_TRIP_FALL_REG1; i <= SN5S330_INT_TRIP_FALL_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr, i, &data);
		ccprintf("INT_TRIP_FALL_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_TRIP_FALL_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_MASK_RISE_REG1; i <= SN5S330_INT_MASK_RISE_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr, i, &data);
		ccprintf("INT_MASK_RISE_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_MASK_RISE_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_MASK_FALL_REG1; i <= SN5S330_INT_MASK_FALL_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr, i, &data);
		ccprintf("INT_MASK_FALL_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_MASK_FALL_REG1 + 1,
			 i,
			 data);
	}

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

static int get_func_set3(uint8_t port, int *regval)
{
	int status;

	status = read_reg(port, SN5S330_FUNC_SET3, regval);
	if (status)
		CPRINTS("ppc p%d: Failed to read FUNC_SET3!", port);

	return status;
}

static int sn5s330_is_pp_fet_enabled(uint8_t port, enum sn5s330_pp_idx pp,
			     int *is_enabled)
{
	int pp_bit;
	int status;
	int regval;

	if (pp == SN5S330_PP1)
		pp_bit = SN5S330_PP1_EN;
	else if (pp == SN5S330_PP2)
		pp_bit = SN5S330_PP2_EN;
	else
		return EC_ERROR_INVAL;

	status = get_func_set3(port, &regval);
	if (status)
		return status;

	*is_enabled = !!(pp_bit & regval);

	return EC_SUCCESS;
}

static int sn5s330_pp_fet_enable(uint8_t port, enum sn5s330_pp_idx pp,
				 int enable)
{
	int regval;
	int status;
	int pp_bit;

	if (pp == SN5S330_PP1)
		pp_bit = SN5S330_PP1_EN;
	else if (pp == SN5S330_PP2)
		pp_bit = SN5S330_PP2_EN;
	else
		return EC_ERROR_INVAL;

	status = get_func_set3(port, &regval);
	if (status)
		return status;

	if (enable)
		regval |= pp_bit;
	else
		regval &= ~pp_bit;

	status = write_reg(port, SN5S330_FUNC_SET3, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET3!", port);
		return status;
	}

	return EC_SUCCESS;
}

static int sn5s330_init(int port)
{
	int regval;
	int status;
	int retries;
	int reg;
	const int i2c_port  = ppc_chips[port].i2c_port;
	const int i2c_addr = ppc_chips[port].i2c_addr;

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Set the sourcing current limit value. */
	switch (CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) {
	case TYPEC_RP_3A0:
		/* Set current limit to ~3A. */
		regval = SN5S330_ILIM_3_06;
		break;

	case TYPEC_RP_1A5:
	default:
		/* Set current limit to ~1.5A. */
		regval = SN5S330_ILIM_1_62;
		break;
	}
#else /* !defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */
	/* Default SRC current limit to ~1.5A. */
	regval = SN5S330_ILIM_1_62;
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/*
	 * It seems that sometimes setting the FUNC_SET1 register fails
	 * initially.  Therefore, we'll retry a couple of times.
	 */
	retries = 0;
	do {
		status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET1,
				    regval);
		if (status) {
			CPRINTS("ppc p%d: Failed to set FUNC_SET1! Retrying..",
				port);
			retries++;
			msleep(1);
		} else {
			break;
		}
	} while (retries < 10);

	/* Set Vbus OVP threshold to ~22.325V. */
	regval = 0x37;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET5, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET5!", port);
		return status;
	}

	/* Set Vbus UVP threshold to ~2.75V. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_FUNC_SET6, &regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to read FUNC_SET6!", port);
		return status;
	}
	regval &= ~0x3F;
	regval |= 1;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET6, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to write FUNC_SET6!", port);
		return status;
	}

	/* Enable SBU Fets and set PP2 current limit to ~3A. */
	regval = SN5S330_SBU_EN | 0xf;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET2, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET2!", port);
		return status;
	}

	/* TODO(aaboagye): What about Vconn? */

	/*
	 * Indicate we are using PP2 configuration 2 and enable OVP comparator
	 * for CC lines.
	 */
	regval = SN5S330_OVP_EN_CC | SN5S330_PP2_CONFIG;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET9, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET9!", port);
		return status;
	}

	/* Set analog current limit delay to 200 us for both PP1 & PP2. */
	regval = (PPX_ILIM_DEGLITCH_0_US_200 << 3) | PPX_ILIM_DEGLITCH_0_US_200;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET11,
			    regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET11", port);
		return status;
	}

	/* Turn off dead battery resistors and turn on CC FETs. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_FUNC_SET4, &regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to read FUNC_SET4!", port);
		return status;
	}
	regval |= SN5S330_CC_EN;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET4, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET4!", port);
		return status;
	}

	/* Set ideal diode mode for both PP1 and PP2. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_FUNC_SET3, &regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to read FUNC_SET3!", port);
		return status;
	}
	regval |= SN5S330_SET_RCP_MODE_PP1 | SN5S330_SET_RCP_MODE_PP2;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET3, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to set FUNC_SET3!", port);
		return status;
	}

	/* Turn off PP1 FET. */
	status = sn5s330_pp_fet_enable(port, SN5S330_PP1, 0);
	if (status) {
		CPRINTS("ppc p%d: Failed to turn off PP1 FET!", port);
	}

	/*
	 * Don't proceed with the rest of initialization if we're sysjumping.
	 * We would have already done this before.
	 */
	if (system_jumped_to_this_image())
		return EC_SUCCESS;

	/*
	 * Clear the digital reset bit, and mask off and clear vSafe0V
	 * interrupts. Leave the dead battery mode bit unchanged since it
	 * is checked below.
	 */
	regval = SN5S330_DIG_RES | SN5S330_VSAFE0V_MASK;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			    regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_STATUS_REG4!", port);
		return status;
	}

	/*
	 * Before turning on the PP2 FET, let's mask off all interrupts except
	 * for the PP1 overcurrent condition and then clear all pending
	 * interrupts. If PPC is being used to detect VBUS, then also enable
	 * interrupts for VBUS presence.
	 *
	 * TODO(aaboagye): Unmask fast-role swap events once fast-role swap is
	 * implemented in the PD stack.
	 */

	regval = ~SN5S330_ILIM_PP1_MASK;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_RISE_REG1,
			    regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_MASK_RISE1!", port);
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_FALL_REG1,
			    regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_MASK_FALL1!", port);
		return status;
	}

	/* Now mask all the other interrupts. */
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_RISE_REG2,
			    0xFF);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_MASK_RISE2!", port);
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_FALL_REG2,
			    0xFF);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_MASK_FALL2!", port);
		return status;
	}

#if defined(CONFIG_USB_PD_VBUS_DETECT_PPC) && defined(CONFIG_USB_CHARGER)
	/* If PPC is being used to detect VBUS, enable VBUS interrupts. */
	regval = ~SN5S330_VBUS_GOOD_MASK;
#else
	regval = 0xFF;
#endif  /* CONFIG_USB_PD_VBUS_DETECT_PPC && CONFIG_USB_CHARGER */

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_RISE_REG3,
			    regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_MASK_RISE3!", port);
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_FALL_REG3,
			    regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to write INT_MASK_FALL3!", port);
		return status;
	}

	/* Now clear any pending interrupts. */
	for (reg = SN5S330_INT_TRIP_RISE_REG1;
	     reg <= SN5S330_INT_TRIP_FALL_REG3;
	     reg++) {
		status = i2c_write8(i2c_port, i2c_addr, reg, 0xFF);
		if (status) {
			CPRINTS("ppc p%d: Failed to write reg 0x%2x!", port);
			return status;
		}
	}


	/*
	 * For PP2, check to see if we booted in dead battery mode.  If we
	 * booted in dead battery mode, the PP2 FET will already be enabled.
	 */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			   &regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to read INT_STATUS_REG4!", port);
		return status;
	}

	if (regval & SN5S330_DB_BOOT) {
		/*
		 * Clear the bit by writing 1 and keep vSafe0V_MASK
		 * unchanged.
		 */
		i2c_write8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			   regval);

		/* Turn on PP2 FET. */
		status = sn5s330_pp_fet_enable(port, SN5S330_PP2, 1);
		if (status) {
			CPRINTS("ppc p%d: Failed to turn on PP2 FET!", port);
			return status;
		}
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int sn5s330_is_vbus_present(int port)
{
	int regval;
	int rv;

	rv = read_reg(port, SN5S330_INT_STATUS_REG3, &regval);
	if (rv) {
		CPRINTS("ppc p%d: VBUS present error (%d)", port, rv);
		return 0;
	}

	return !!(regval & SN5S330_VBUS_GOOD);
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */

static int sn5s330_is_sourcing_vbus(int port)
{
	int is_sourcing_vbus = 0;
	int rv;

	rv = sn5s330_is_pp_fet_enabled(port, SN5S330_PP1, &is_sourcing_vbus);
	if (rv) {
		CPRINTS("ppc p%d: Failed to determine source FET status! (%d)",
			port, rv);
		return 0;
	}

	return is_sourcing_vbus;
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int sn5s330_set_polarity(int port, int polarity)
{
	int regval;
	int status;

	status = read_reg(port, SN5S330_FUNC_SET4, &regval);
	if (status)
		return status;

	if (polarity)
		regval |= SN5S330_CC_POLARITY; /* CC2 active. */
	else
		regval &= ~SN5S330_CC_POLARITY; /* CC1 active. */

	return write_reg(port, SN5S330_FUNC_SET4, regval);
}
#endif

static int sn5s330_set_vbus_source_current_limit(int port,
						 enum tcpc_rp_value rp)
{
	int regval;
	int status;

	status = read_reg(port, SN5S330_FUNC_SET1, &regval);
	if (status)
		return status;

	/*
	 * Note that we chose the lowest current limit setting that is just
	 * above indicated Rp value.  This is because these are minimum values
	 * and we must be able to provide the current that we advertise.
	 */
	regval &= ~0x1F; /* The current limit settings are 4:0. */
	switch (rp) {
	case TYPEC_RP_3A0:
		regval |= SN5S330_ILIM_3_06;
		break;

	case TYPEC_RP_1A5:
		regval |= SN5S330_ILIM_1_62;
		break;

	case TYPEC_RP_USB:
	default:
		regval |= SN5S330_ILIM_0_63;
		break;
	};

	status = write_reg(port, SN5S330_FUNC_SET1, regval);

	return status;
}

static int sn5s330_discharge_vbus(int port, int enable)
{
	int regval;
	int status;

	status = get_func_set3(port, &regval);
	if (status)
		return status;

	if (enable)
		regval |= SN5S330_VBUS_DISCH_EN;
	else
		regval &= ~SN5S330_VBUS_DISCH_EN;

	status = write_reg(port, SN5S330_FUNC_SET3, regval);
	if (status) {
		CPRINTS("ppc p%d: Failed to %s vbus discharge",
			port, enable ? "enable" : "disable");
		return status;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_USBC_PPC_VCONN
static int sn5s330_set_vconn(int port, int enable)
{
	int regval;
	int status;

	status = read_reg(port, SN5S330_FUNC_SET4, &regval);
	if (status)
		return status;

	if (enable)
		regval |= SN5S330_VCONN_EN;
	else
		regval &= ~SN5S330_VCONN_EN;

	return write_reg(port, SN5S330_FUNC_SET4, regval);
}
#endif

static int sn5s330_vbus_sink_enable(int port, int enable)
{
	return sn5s330_pp_fet_enable(port, SN5S330_PP2, !!enable);
}

static int sn5s330_vbus_source_enable(int port, int enable)
{
	return sn5s330_pp_fet_enable(port, SN5S330_PP1, !!enable);
}

static void sn5s330_handle_interrupt(int port)
{
	int rise = 0;
	int fall = 0;

	/*
	 * The only interrupts that should be enabled are the PP1 overcurrent
	 * condition, and for VBUS_GOOD if PPC is being used to detect VBUS.
	 */
	read_reg(port, SN5S330_INT_TRIP_RISE_REG1, &rise);
	read_reg(port, SN5S330_INT_TRIP_FALL_REG1, &fall);

	/* Let the board know about the overcurrent event. */
	if (rise & SN5S330_ILIM_PP1_MASK)
		board_overcurrent_event(port);

	/* Clear the interrupt sources. */
	write_reg(port, SN5S330_INT_TRIP_RISE_REG1, rise);
	write_reg(port, SN5S330_INT_TRIP_FALL_REG1, fall);

#if defined(CONFIG_USB_PD_VBUS_DETECT_PPC) && defined(CONFIG_USB_CHARGER)
	read_reg(port, SN5S330_INT_TRIP_RISE_REG3, &rise);
	read_reg(port, SN5S330_INT_TRIP_FALL_REG3, &fall);

	/* Inform other modules about VBUS level */
	if (rise & SN5S330_VBUS_GOOD_MASK
	    || fall & SN5S330_VBUS_GOOD_MASK)
		usb_charger_vbus_change(port, sn5s330_is_vbus_present(port));

	/* Clear the interrupt sources. */
	write_reg(port, SN5S330_INT_TRIP_RISE_REG3, rise);
	write_reg(port, SN5S330_INT_TRIP_FALL_REG3, fall);
#endif  /* CONFIG_USB_PD_VBUS_DETECT_PPC && CONFIG_USB_CHARGER */
}

static void sn5s330_irq_deferred(void)
{
	int i;
	uint32_t pending = atomic_read_clear(&irq_pending);

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++)
		if ((1 << i) & pending)
			sn5s330_handle_interrupt(i);
}
DECLARE_DEFERRED(sn5s330_irq_deferred);

void sn5s330_interrupt(int port)
{
	atomic_or(&irq_pending, (1 << port));
	hook_call_deferred(&sn5s330_irq_deferred_data, 0);
}

const struct ppc_drv sn5s330_drv = {
	.init = &sn5s330_init,
	.is_sourcing_vbus = &sn5s330_is_sourcing_vbus,
	.vbus_sink_enable = &sn5s330_vbus_sink_enable,
	.vbus_source_enable = &sn5s330_vbus_source_enable,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &sn5s330_dump,
#endif /* defined(CONFIG_CMD_PPC_DUMP) */
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &sn5s330_is_vbus_present,
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &sn5s330_set_polarity,
#endif
	.set_vbus_source_current_limit = &sn5s330_set_vbus_source_current_limit,
	.discharge_vbus = &sn5s330_discharge_vbus,
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &sn5s330_set_vconn,
#endif
};
