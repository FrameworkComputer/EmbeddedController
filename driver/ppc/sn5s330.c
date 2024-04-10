/* Copyright 2017 The ChromiumOS Authors
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
#include "hooks.h"
#include "i2c.h"
#include "sn5s330.h"
#include "system.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static atomic_t irq_pending; /* Bitmask of ports signaling an interrupt. */
static int source_enabled[CONFIG_USB_PD_PORT_MAX_COUNT];

static int read_reg(uint8_t port, int reg, int *regval)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr_flags, reg, regval);
}

static int write_reg(uint8_t port, int reg, int regval)
{
	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr_flags, reg, regval);
}

static int set_flags(const int port, const int addr, const int flags_to_set)
{
	int val, rv;

	rv = read_reg(port, addr, &val);
	if (rv)
		return rv;

	val |= flags_to_set;

	return write_reg(port, addr, val);
}

static int clr_flags(const int port, const int addr, const int flags_to_clear)
{
	int val, rv;

	rv = read_reg(port, addr, &val);
	if (rv)
		return rv;

	val &= ~flags_to_clear;

	return write_reg(port, addr, val);
}

#ifdef CONFIG_CMD_PPC_DUMP
static int sn5s330_dump(int port)
{
	int i;
	int data;
	const int i2c_port = ppc_chips[port].i2c_port;
	const uint16_t i2c_addr_flags = ppc_chips[port].i2c_addr_flags;

	/* Flush after every set otherwise console buffer may get full. */

	for (i = SN5S330_FUNC_SET1; i <= SN5S330_FUNC_SET12; i++) {
		i2c_read8(i2c_port, i2c_addr_flags, i, &data);
		ccprintf("FUNC_SET%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_FUNC_SET1 + 1, i, data);
	}

	cflush();

	for (i = SN5S330_INT_STATUS_REG1; i <= SN5S330_INT_STATUS_REG4; i++) {
		i2c_read8(i2c_port, i2c_addr_flags, i, &data);
		ccprintf("INT_STATUS_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_STATUS_REG1 + 1, i, data);
	}

	cflush();

	for (i = SN5S330_INT_TRIP_RISE_REG1; i <= SN5S330_INT_TRIP_RISE_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr_flags, i, &data);
		ccprintf("INT_TRIP_RISE_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_TRIP_RISE_REG1 + 1, i, data);
	}

	cflush();

	for (i = SN5S330_INT_TRIP_FALL_REG1; i <= SN5S330_INT_TRIP_FALL_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr_flags, i, &data);
		ccprintf("INT_TRIP_FALL_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_TRIP_FALL_REG1 + 1, i, data);
	}

	cflush();

	for (i = SN5S330_INT_MASK_RISE_REG1; i <= SN5S330_INT_MASK_RISE_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr_flags, i, &data);
		ccprintf("INT_MASK_RISE_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_MASK_RISE_REG1 + 1, i, data);
	}

	cflush();

	for (i = SN5S330_INT_MASK_FALL_REG1; i <= SN5S330_INT_MASK_FALL_REG3;
	     i++) {
		i2c_read8(i2c_port, i2c_addr_flags, i, &data);
		ccprintf("INT_MASK_FALL_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_MASK_FALL_REG1 + 1, i, data);
	}

	cflush();

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

static int sn5s330_pp_fet_enable(uint8_t port, enum sn5s330_pp_idx pp,
				 int enable)
{
	int status;
	int pp_bit;

	if (pp == SN5S330_PP1)
		pp_bit = SN5S330_PP1_EN;
	else if (pp == SN5S330_PP2)
		pp_bit = SN5S330_PP2_EN;
	/* LCOV_EXCL_START - this branch unreachable in unit tests. */
	else
		return EC_ERROR_INVAL;
	/* LCOV_EXCL_STOP */

	status = enable ? set_flags(port, SN5S330_FUNC_SET3, pp_bit) :
			  clr_flags(port, SN5S330_FUNC_SET3, pp_bit);

	if (status) {
		ppc_prints("Failed to set FUNC_SET3!", port);
		return status;
	}

	if (pp == SN5S330_PP1)
		source_enabled[port] = enable;

	return EC_SUCCESS;
}

static int sn5s330_init(int port)
{
	int regval;
	int status;
	int retries;
	int reg;
	const int i2c_port = ppc_chips[port].i2c_port;
	const uint16_t i2c_addr_flags = ppc_chips[port].i2c_addr_flags;

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
		status = i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET1,
				    regval);
		if (status) {
			ppc_prints("Failed to set FUNC_SET1! Retrying..", port);
			retries++;
			crec_msleep(1);
		} else {
			break;
		}
	} while (retries < 10);

	/* Set Vbus OVP threshold to ~22.325V. */
	regval = 0x37;
	status =
		i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET5, regval);
	if (status) {
		ppc_prints("Failed to set FUNC_SET5!", port);
		return status;
	}

	/* Set Vbus UVP threshold to ~2.75V. */
	status =
		i2c_read8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET6, &regval);
	if (status) {
		ppc_prints("Failed to read FUNC_SET6!", port);
		return status;
	}
	regval &= ~0x3F;
	regval |= 1;
	status =
		i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET6, regval);
	if (status) {
		ppc_prints("Failed to write FUNC_SET6!", port);
		return status;
	}

	/* Enable SBU Fets and set PP2 current limit to ~3A. */
	regval = SN5S330_SBU_EN | 0x8;
	status =
		i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET2, regval);
	if (status) {
		ppc_prints("Failed to set FUNC_SET2!", port);
		return status;
	}

	/*
	 * Indicate we are using PP2 configuration 2 and enable OVP comparator
	 * for CC lines.
	 *
	 * Also, turn off under-voltage protection for incoming Vbus as it would
	 * prevent us from enabling SNK path before we hibernate the ec. We
	 * need to enable the SNK path so USB power will assert ACOK and wake
	 * the EC up went inserting USB power. We always turn off under-voltage
	 * protection because the battery charger will boost the voltage up
	 * to the needed battery voltage either way (and it will have its own
	 * low voltage protection).
	 */
	regval = SN5S330_OVP_EN_CC | SN5S330_PP2_CONFIG | SN5S330_CONFIG_UVP;
	status =
		i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET9, regval);
	if (status) {
		ppc_prints("Failed to set FUNC_SET9!", port);
		return status;
	}

	/*
	 * Set analog current limit delay to 200 us for PP1,
	 * set 1000 us for PP2 for compatibility.
	 */
	regval = (PPX_ILIM_DEGLITCH_0_US_200 << 3) |
		 PPX_ILIM_DEGLITCH_0_US_1000;
	status = i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET11,
			    regval);
	if (status) {
		ppc_prints("Failed to set FUNC_SET11", port);
		return status;
	}

#ifdef CONFIG_USBC_PPC_VCONN
	/*
	 * Set the deglitch timeout on the Vconn current limit to 640us.  This
	 * improves compatibility with some USB C -> HDMI devices versus the
	 * reset default (20 us).
	 */
	regval = 0;
	status =
		i2c_read8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET8, &regval);
	if (status) {
		ppc_prints("Failed to read FUNC_SET8!", port);
		return status;
	}
	regval &= ~SN5S330_VCONN_DEGLITCH_MASK;
	regval |= SN5S330_VCONN_DEGLITCH_640_US;
	status =
		i2c_write8(i2c_port, i2c_addr_flags, SN5S330_FUNC_SET8, regval);
	if (status) {
		ppc_prints("Failed to set FUNC_SET8!", port);
		return status;
	}
#endif /* CONFIG_USBC_PPC_VCONN */

	/*
	 * Turn off dead battery resistors, turn on CC FETs, and set the higher
	 * of the two VCONN current limits (min 0.6A).  Many VCONN accessories
	 * trip the default current limit of min 0.35A.
	 */
	status = set_flags(port, SN5S330_FUNC_SET4,
			   SN5S330_CC_EN | SN5S330_VCONN_ILIM_SEL);
	if (status) {
		ppc_prints("Failed to set FUNC_SET4!", port);
		return status;
	}

	/* Set ideal diode mode for both PP1 and PP2. */
	status = set_flags(port, SN5S330_FUNC_SET3,
			   SN5S330_SET_RCP_MODE_PP1 | SN5S330_SET_RCP_MODE_PP2);
	if (status) {
		ppc_prints("Failed to set FUNC_SET3!", port);
		return status;
	}

	/*
	 * Set RCP voltage threshold to 3mV instead of 6mV default for the
	 * source path. This modification helps prevent false RCP triggers
	 * against certain port partners when VBUS is set to 20V.
	 */
	status = clr_flags(port, SN5S330_FUNC_SET10, SN5S330_PP1_RCP_OFFSET);
	if (status) {
		ppc_prints("Failed to set FUNC_SET10!", port);
		return status;
	}

	/* Turn off PP1 FET. */
	status = sn5s330_pp_fet_enable(port, SN5S330_PP1, 0);
	if (status) {
		ppc_prints("Failed to turn off PP1 FET!", port);
		return status;
	}

	/*
	 * Don't proceed with the rest of initialization if we're sysjumping.
	 * We would have already done this before.
	 */
	if (system_jumped_late())
		return EC_SUCCESS;

	/*
	 * Clear the digital reset bit, and mask off and clear vSafe0V
	 * interrupts. Leave the dead battery mode bit unchanged since it
	 * is checked below.
	 */
	regval = SN5S330_DIG_RES | SN5S330_VSAFE0V_MASK;
	status = i2c_write8(i2c_port, i2c_addr_flags, SN5S330_INT_STATUS_REG4,
			    regval);
	if (status) {
		ppc_prints("Failed to write INT_STATUS_REG4!", port);
		return status;
	}

	/*
	 * Before turning on the PP2 FET, mask off all unwanted interrupts and
	 * then clear all pending interrupts.
	 *
	 * TODO(aaboagye): Unmask fast-role swap events once fast-role swap is
	 * implemented in the PD stack.
	 */

	/* Enable PP1 overcurrent interrupts. */
	regval = ~SN5S330_ILIM_PP1_MASK;
	status = i2c_write8(i2c_port, i2c_addr_flags,
			    SN5S330_INT_MASK_RISE_REG1, regval);
	if (status) {
		ppc_prints("Failed to write INT_MASK_RISE1!", port);
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr_flags,
			    SN5S330_INT_MASK_FALL_REG1, 0xFF);
	if (status) {
		ppc_prints("Failed to write INT_MASK_FALL1!", port);
		return status;
	}

	/* Enable VCONN overcurrent and CC1/CC2 overvoltage interrupts. */
	regval = ~(SN5S330_VCONN_ILIM | SN5S330_CC1_CON | SN5S330_CC2_CON);
	status = i2c_write8(i2c_port, i2c_addr_flags,
			    SN5S330_INT_MASK_RISE_REG2, regval);
	if (status) {
		ppc_prints("Failed to write INT_MASK_RISE2!", port);
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr_flags,
			    SN5S330_INT_MASK_FALL_REG2, 0xFF);
	if (status) {
		ppc_prints("Failed to write INT_MASK_FALL2!", port);
		return status;
	}

#if defined(CONFIG_USB_PD_VBUS_DETECT_PPC) && defined(CONFIG_USB_CHARGER)
	/* If PPC is being used to detect VBUS, enable VBUS interrupts. */
	regval = ~SN5S330_VBUS_GOOD_MASK;
#else
	regval = 0xFF;
#endif /* CONFIG_USB_PD_VBUS_DETECT_PPC && CONFIG_USB_CHARGER */

	status = i2c_write8(i2c_port, i2c_addr_flags,
			    SN5S330_INT_MASK_RISE_REG3, regval);
	if (status) {
		ppc_prints("Failed to write INT_MASK_RISE3!", port);
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr_flags,
			    SN5S330_INT_MASK_FALL_REG3, regval);
	if (status) {
		ppc_prints("Failed to write INT_MASK_FALL3!", port);
		return status;
	}

	/* Now clear any pending interrupts. */
	for (reg = SN5S330_INT_TRIP_RISE_REG1;
	     reg <= SN5S330_INT_TRIP_FALL_REG3; reg++) {
		status = i2c_write8(i2c_port, i2c_addr_flags, reg, 0xFF);
		if (status) {
			CPRINTS("ppc p%d: Failed to write reg 0x%2x!", port,
				reg);
			return status;
		}
	}

	/*
	 * For PP2, check to see if we booted in dead battery mode.  If we
	 * booted in dead battery mode, the PP2 FET will already be enabled.
	 */
	status = i2c_read8(i2c_port, i2c_addr_flags, SN5S330_INT_STATUS_REG4,
			   &regval);
	if (status) {
		ppc_prints("Failed to read INT_STATUS_REG4!", port);
		return status;
	}

	if (regval & SN5S330_DB_BOOT) {
		/*
		 * Clear the bit by writing 1 and keep vSafe0V_MASK
		 * unchanged.
		 */
		i2c_write8(i2c_port, i2c_addr_flags, SN5S330_INT_STATUS_REG4,
			   regval);

		/*
		 * Turn on PP2 FET.
		 * Although PP2 FET is already enabled during dead batter boot
		 * by the spec, we force that state here.
		 *
		 * TODO(207034759): Verify need or remove redundant PP2 set.
		 */

		status = sn5s330_pp_fet_enable(port, SN5S330_PP2, 1);
		if (status) {
			ppc_prints("Failed to turn on PP2 FET!", port);
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
		ppc_err_prints("VBUS present error", port, rv);
		return 0;
	}

	return !!(regval & SN5S330_VBUS_GOOD);
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */

static int sn5s330_is_sourcing_vbus(int port)
{
	return source_enabled[port];
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int sn5s330_set_polarity(int port, int polarity)
{
	if (polarity)
		/* CC2 active. */
		return set_flags(port, SN5S330_FUNC_SET4, SN5S330_CC_POLARITY);
	else
		/* CC1 active. */
		return clr_flags(port, SN5S330_FUNC_SET4, SN5S330_CC_POLARITY);
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

	/* USB minimum source current is 0.5A */
	case TYPEC_RP_USB:
	default:
		/* SN5S330 Defaults to USB associated limits */
		regval |= SN5S330_ILIM_0_63;
		break;
	};

	status = write_reg(port, SN5S330_FUNC_SET1, regval);

	return status;
}

static int sn5s330_discharge_vbus(int port, int enable)
{
	int status = enable ? set_flags(port, SN5S330_FUNC_SET3,
					SN5S330_VBUS_DISCH_EN) :
			      clr_flags(port, SN5S330_FUNC_SET3,
					SN5S330_VBUS_DISCH_EN);

	if (status) {
		CPRINTS("ppc p%d: Failed to %s vbus discharge", port,
			enable ? "enable" : "disable");
		return status;
	}

	return EC_SUCCESS;
}

static int sn5s330_enter_low_power_mode(int port)
{
	int rv;

	/* Turn off both SRC and SNK FETs */
	rv = clr_flags(port, SN5S330_FUNC_SET3,
		       SN5S330_PP1_EN | SN5S330_PP2_EN);

	if (rv) {
		ppc_err_prints("Could not disable both FETS", port, rv);
		return rv;
	}

	/* Turn off Vconn power */
	rv = clr_flags(port, SN5S330_FUNC_SET4, SN5S330_VCONN_EN);

	if (rv) {
		ppc_err_prints("Could not disable Vconn", port, rv);
		return rv;
	}

	/* Turn off SBU path */
	rv = clr_flags(port, SN5S330_FUNC_SET2, SN5S330_SBU_EN);

	if (rv) {
		ppc_err_prints("Could not disable SBU path", port, rv);
		return rv;
	}

	/*
	 * Turn off the Over Voltage Protection circuits. Needs to happen after
	 * FETs are disabled, otherwise OVP can automatically turned back on.
	 * Since FETs are off, any over voltage does not make it to the board
	 * side of the PPC.
	 */
	rv = clr_flags(port, SN5S330_FUNC_SET9,
		       SN5S330_FORCE_OVP_EN_SBU | SN5S330_FORCE_ON_VBUS_OVP |
			       SN5S330_FORCE_ON_VBUS_UVP);

	if (rv) {
		ppc_err_prints("Could not disable OVP circuit", port, rv);
		return rv;
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

#ifdef CONFIG_USBC_PPC_SBU
static int sn5s330_set_sbu(int port, int enable)
{
	int rv;

	if (enable)
		rv = set_flags(port, SN5S330_FUNC_SET2, SN5S330_SBU_EN);
	else
		rv = clr_flags(port, SN5S330_FUNC_SET2, SN5S330_SBU_EN);

	return rv;
}
#endif /* CONFIG_USBC_PPC_SBU */

static void sn5s330_handle_interrupt(int port)
{
	int attempt = 0;

	/*
	 * SN5S330's /INT pin is level, so process interrupts until it
	 * deasserts if the chip has a dedicated interrupt pin.
	 */
#ifdef CONFIG_USBC_PPC_DEDICATED_INT
	while (ppc_get_alert_status(port))
#endif
	{
		int rise = 0;
		int fall = 0;

		attempt++;

		if (attempt > 1)
			ppc_prints("Could not clear interrupts on first "
				   "try, retrying",
				   port);

		read_reg(port, SN5S330_INT_TRIP_RISE_REG1, &rise);
		read_reg(port, SN5S330_INT_TRIP_FALL_REG1, &fall);

		/* Notify the system about the overcurrent event. */
		if (rise & SN5S330_ILIM_PP1_MASK)
			pd_handle_overcurrent(port);

		/* Clear the interrupt sources. */
		write_reg(port, SN5S330_INT_TRIP_RISE_REG1, rise);
		write_reg(port, SN5S330_INT_TRIP_FALL_REG1, fall);

		read_reg(port, SN5S330_INT_TRIP_RISE_REG2, &rise);
		read_reg(port, SN5S330_INT_TRIP_FALL_REG2, &fall);

		/*
		 * VCONN may be latched off due to an overcurrent.  Indicate
		 * when the VCONN overcurrent happens.
		 */
		if (rise & SN5S330_VCONN_ILIM)
			ppc_prints("VCONN OC!", port);

		/* Notify the system about the CC overvoltage event. */
		if (rise & SN5S330_CC1_CON || rise & SN5S330_CC2_CON) {
			ppc_prints("CC OV!", port);
			pd_handle_cc_overvoltage(port);
		}

		/* Clear the interrupt sources. */
		write_reg(port, SN5S330_INT_TRIP_RISE_REG2, rise);
		write_reg(port, SN5S330_INT_TRIP_FALL_REG2, fall);

#if defined(CONFIG_USB_PD_VBUS_DETECT_PPC) && defined(CONFIG_USB_CHARGER)
		read_reg(port, SN5S330_INT_TRIP_RISE_REG3, &rise);
		read_reg(port, SN5S330_INT_TRIP_FALL_REG3, &fall);

		/* Inform other modules about VBUS level */
		if (rise & SN5S330_VBUS_GOOD_MASK ||
		    fall & SN5S330_VBUS_GOOD_MASK)
			usb_charger_vbus_change(port,
						sn5s330_is_vbus_present(port));

		/* Clear the interrupt sources. */
		write_reg(port, SN5S330_INT_TRIP_RISE_REG3, rise);
		write_reg(port, SN5S330_INT_TRIP_FALL_REG3, fall);
#endif /* CONFIG_USB_PD_VBUS_DETECT_PPC && CONFIG_USB_CHARGER */
	}
}

static void sn5s330_irq_deferred(void)
{
	int i;
	uint32_t pending = atomic_clear(&irq_pending);

	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		if (BIT(i) & pending)
			sn5s330_handle_interrupt(i);
}
DECLARE_DEFERRED(sn5s330_irq_deferred);

void sn5s330_interrupt(int port)
{
	atomic_or(&irq_pending, BIT(port));
	hook_call_deferred(&sn5s330_irq_deferred_data, 0);
}

const struct ppc_drv sn5s330_drv = {
	.init = &sn5s330_init,
	.is_sourcing_vbus = &sn5s330_is_sourcing_vbus,
	.vbus_sink_enable = &sn5s330_vbus_sink_enable,
	.vbus_source_enable = &sn5s330_vbus_source_enable,
	.set_vbus_source_current_limit = &sn5s330_set_vbus_source_current_limit,
	.discharge_vbus = &sn5s330_discharge_vbus,
	.enter_low_power_mode = &sn5s330_enter_low_power_mode,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &sn5s330_dump,
#endif
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &sn5s330_is_vbus_present,
#endif
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &sn5s330_set_polarity,
#endif
#ifdef CONFIG_USBC_PPC_SBU
	.set_sbu = &sn5s330_set_sbu,
#endif /* defined(CONFIG_USBC_PPC_SBU) */
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &sn5s330_set_vconn,
#endif
	.interrupt = &sn5s330_interrupt,
};
