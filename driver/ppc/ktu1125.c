/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kinetic KTU1125 USB-C Power Path Controller */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "ktu1125.h"
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

static void ktu1125_handle_interrupt(int port);

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

static int set_field(const int port, const int addr, const int shift,
		     const int field_length, const int field_to_set)
{
	int reg_val, new_reg_val;
	int field_val, field_mask;
	int rv;

	rv = read_reg(port, addr, &reg_val);
	if (rv != EC_SUCCESS)
		return rv;

	field_mask = (BIT(field_length) - 1) << shift;
	field_val = (field_to_set << shift) & field_mask;

	new_reg_val = reg_val;
	new_reg_val &= ~field_mask;
	new_reg_val |= field_val;

	if (new_reg_val == reg_val)
		return EC_SUCCESS;

	return write_reg(port, addr, new_reg_val);
}

#ifdef CONFIG_CMD_PPC_DUMP
static int ktu1125_dump(int port)
{
	int i;
	int data;
	CPRINTF("PPC%d: KTU1125. Registers:\n", port);

	for (i = KTU1125_ID; i <= KTU1125_INT_DATA; i++) {
		read_reg(port, i, &data);
		CPRINTF("REG %02Xh = 0x%02x\n", i, data);
	}

	cflush();
	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

/* helper */
static int ktu1125_power_path_control(int port, int enable)
{
	int status =
		enable ?
			set_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_SW_AB_EN) :
			clr_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_SW_AB_EN);

	if (status) {
		CPRINTS("ppc p%d: Failed to %s power path", port,
			enable ? "enable" : "disable");
	}

	return status;
}

static int ktu1125_init(int port)
{
	int regval;
	int ctrl_sw_cfg = 0;
	int set_sw_cfg = 0;
	int set_sw2_cfg = 0;
	int sysb_clp;
	int status;

	/* Read and verify KTU1125 Vendor and Chip ID */
	status = read_reg(port, KTU1125_ID, &regval);

	if (status) {
		ppc_prints("Failed to read device ID!", port);
		return status;
	}

	if (regval != KTU1125_VENDOR_DIE_IDS) {
		ppc_err_prints("KTU1125 ID mismatch!", port, regval);
		return regval;
	}

	/*
	 * Setting control register CTRL_SW_CFG
	 */

	/* Check if VBUS is present and set SW_AB_EN accordingly */
	status = read_reg(port, KTU1125_MONITOR_SNK, &regval);
	if (status) {
		ppc_err_prints("VBUS present error", port, status);
		return 0;
	}

	if (regval & KTU1125_SYSA_OK)
		ctrl_sw_cfg = KTU1125_SW_AB_EN;

	status = write_reg(port, KTU1125_CTRL_SW_CFG, ctrl_sw_cfg);
	if (status) {
		ppc_err_prints("Failed to write CTRL_SW_CFG!", port, status);
		return status;
	}

	/*
	 * Setting control register SET_SW_CFG
	 */

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	/* Set the sourcing current limit value */
	switch (CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) {
	case TYPEC_RP_3A0:
		/* Set current limit to ~3A */
		sysb_clp = KTU1125_SYSB_ILIM_3_30;
		break;

	case TYPEC_RP_1A5:
	default:
		/* Set current limit to ~1.5A */
		sysb_clp = KTU1125_SYSB_ILIM_1_70;
		break;
	}
#else /* !defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */
	/* Default SRC current limit to ~1.5A */
	sysb_clp = KTU1125_SYSB_ILIM_1_70;
#endif /* defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) */

	/* Set SYSB Current Limit Protection */
	set_sw_cfg |= sysb_clp << KTU1125_SYSB_CLP_SHIFT;
	/* Set VCONN Current Limit Protection.
	 * Note: might be changed to 600mA in future
	 */
	set_sw_cfg |= KTU1125_VCONN_ILIM_0_40 << KTU1125_VCONN_CLP_SHIFT;
	/* Disable Dead Battery resistance, because CC FETs are ON */
	set_sw_cfg |= KTU1125_RDB_DIS;

	status = write_reg(port, KTU1125_SET_SW_CFG, set_sw_cfg);
	if (status) {
		ppc_err_prints("Failed to write SET_SW_CFG!", port, status);
		return status;
	}

	/*
	 * Setting control register SET_SW2_CFG
	 */

	/* Set T_HIC */
	set_sw2_cfg |= (KTU_T_HIC_MS_17 << KTU1125_T_HIC_SHIFT);
	/* Set vbus discharge resistance */
	set_sw2_cfg |= (KTU1125_DIS_RES_1400 << KTU1125_DIS_RES_SHIFT);
	/*
	 * Set the over voltage protection to the maximum (25V) to support
	 * sinking from a 20V PD charger. The common PPC code doesn't provide
	 * any hooks for indicating what the currently negotiated voltage is
	 */
	set_sw2_cfg |= (KTU1125_SYSB_VLIM_25_00 << KTU1125_OVP_BUS_SHIFT);

	status = write_reg(port, KTU1125_SET_SW2_CFG, set_sw2_cfg);
	if (status) {
		ppc_err_prints("Failed to write SET_SW2_CFG!", port, status);
		return status;
	}

	/*
	 * Don't proceed with the rest of initialization if we're sysjumping.
	 * We would have already done this before
	 */
	if (system_jumped_late())
		return EC_SUCCESS;

	/*
	 * Enable interrupts
	 */

	/* Leave SYSA_OK and FRS masked for SNK group of interrupts */
	regval = KTU1125_SNK_MASK_ALL & (KTU1125_SYSA_OK | KTU1125_FR_SWAP);
	status = write_reg(port, KTU1125_INTMASK_SNK, regval);
	if (status) {
		ppc_err_prints("Failed to write INTMASK_SNK!", port, status);
		return status;
	}

	/* Only leave VBUS_OK masked for SRC group of interrupts */
	regval = KTU1125_SRC_MASK_ALL & KTU1125_VBUS_OK;
	status = write_reg(port, KTU1125_INTMASK_SRC, regval);
	if (status) {
		ppc_err_prints("Failed to write INTMASK_SRC!", port, status);
		return status;
	}

	/* Unmask the entire DATA group of interrupts */
	status = write_reg(port, KTU1125_INTMASK_DATA, ~KTU1125_DATA_MASK_ALL);
	if (status) {
		ppc_err_prints("Failed to write INTMASK_DATA!", port, status);
		return status;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int ktu1125_is_vbus_present(int port)
{
	int regval;
	int rv;

	rv = read_reg(port, KTU1125_MONITOR_SNK, &regval);
	if (rv) {
		ppc_err_prints("VBUS present error", port, rv);
		return 0;
	}

	return !!(regval & KTU1125_SYSA_OK);
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */

static int ktu1125_is_sourcing_vbus(int port)
{
	int regval;
	int rv;

	rv = read_reg(port, KTU1125_MONITOR_SRC, &regval);
	if (rv) {
		ppc_err_prints("Sourcing VBUS error", port, rv);
		return 0;
	}

	return !!(regval & KTU1125_VBUS_OK);
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int ktu1125_set_polarity(int port, int polarity)
{
	/*
	 * KTU1125 doesn't need to be informed about polarity.
	 * Polarity is queried via pd_get_polarity when applying VCONN.
	 */
	ppc_prints("KTU1125 sets polarity only when applying VCONN", port);
	return EC_SUCCESS;
}
#endif

static int ktu1125_set_vbus_src_current_limit(int port, enum tcpc_rp_value rp)
{
	int regval;
	int status;

	/*
	 * Note that we chose the lowest current limit setting that is just
	 * above indicated Rp value. This is because these are minimum values
	 * and we must be able to provide the current that we advertise
	 */
	switch (rp) {
	case TYPEC_RP_3A0:
		regval = KTU1125_SYSB_ILIM_3_30;
		break;

	case TYPEC_RP_1A5:
		regval = KTU1125_SYSB_ILIM_1_70;
		break;

	case TYPEC_RP_USB:
	default:
		regval = KTU1125_SYSB_ILIM_0_6;
		break;
	};

	status = set_field(port, KTU1125_SET_SW_CFG, KTU1125_SYSB_CLP_SHIFT,
			   KTU1125_SYSB_CLP_LEN, regval);
	if (status)
		ppc_prints("Failed to set KTU1125_SET_SW_CFG!", port);

	return status;
}

static int ktu1125_discharge_vbus(int port, int enable)
{
	int status = enable ? set_flags(port, KTU1125_SET_SW2_CFG,
					KTU1125_VBUS_DIS_EN) :
			      clr_flags(port, KTU1125_SET_SW2_CFG,
					KTU1125_VBUS_DIS_EN);

	if (status) {
		CPRINTS("ppc p%d: Failed to %s vbus discharge", port,
			enable ? "enable" : "disable");
		return status;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_USBC_PPC_VCONN
static int ktu1125_set_vconn(int port, int enable)
{
	int polarity;
	int status;
	int flags = KTU1125_VCONN_EN;

	polarity = polarity_rm_dts(pd_get_polarity(port));

	if (enable) {
		/*
		 * If polarity is CC1, then apply VCONN on CC2.
		 * else if polarity is CC2, then apply VCONN on CC1
		 */
		flags |= polarity ? KTU1125_CC1S_VCONN : KTU1125_CC2S_VCONN;
		status = set_flags(port, KTU1125_CTRL_SW_CFG, flags);
	} else {
		flags |= KTU1125_CC1S_VCONN | KTU1125_CC2S_VCONN;
		status = clr_flags(port, KTU1125_CTRL_SW_CFG, flags);
	}

	return status;
}
#endif

#ifdef CONFIG_USB_PD_FRS_PPC
static int ktu1125_set_frs_enable(int port, int enable)
{
	/* Enable/Disable FR_SWAP Interrupt */
	int status =
		enable ? clr_flags(port, KTU1125_INTMASK_SNK, KTU1125_FR_SWAP) :
			 set_flags(port, KTU1125_INTMASK_SNK, KTU1125_FR_SWAP);

	if (status) {
		ppc_prints("Failed to write KTU1125_INTMASK_SNK!", port);
		return status;
	}

	/* Set the FRS_EN bit */
	status = enable ? set_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_FRS_EN) :
			  clr_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_FRS_EN);

	return status;
}
#endif

static int ktu1125_vbus_sink_enable(int port, int enable)
{
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	/* Skip if VBUS SNK is already enabled/disabled */
	if (ktu1125_is_vbus_present(port) == enable)
		return EC_SUCCESS;
#endif

	/* Select active sink */
	int rv = clr_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_POW_MODE);

	if (rv) {
		ppc_err_prints("Could not select SNK path", port, rv);
		return rv;
	}

	return ktu1125_power_path_control(port, enable);
}

static int ktu1125_vbus_source_enable(int port, int enable)
{
	/* Skip if VBUS SRC is already enabled/disabled */
	if (ktu1125_is_sourcing_vbus(port) == enable)
		return EC_SUCCESS;

	/* Select active source */
	int rv = set_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_POW_MODE);

	if (rv) {
		ppc_err_prints("Could not select SRC path", port, rv);
		return rv;
	}

	return ktu1125_power_path_control(port, enable);
}

#ifdef CONFIG_USBC_PPC_SBU
static int ktu1125_set_sbu(int port, int enable)
{
	int status =
		enable ?
			clr_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_SBU_SHUT) :
			set_flags(port, KTU1125_CTRL_SW_CFG, KTU1125_SBU_SHUT);

	if (status) {
		CPRINTS("ppc p%d: Failed to %s sbu", port,
			enable ? "enable" : "disable");
	}

	return status;
}
#endif /* CONFIG_USBC_PPC_SBU */

static void ktu1125_irq_deferred(void)
{
	int i;
	uint32_t pending = atomic_clear(&irq_pending);

	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		if (BIT(i) & pending)
			ktu1125_handle_interrupt(i);
}
DECLARE_DEFERRED(ktu1125_irq_deferred);

void ktu1125_interrupt(int port)
{
	atomic_or(&irq_pending, BIT(port));
	hook_call_deferred(&ktu1125_irq_deferred_data, 0);
}

static void ktu1125_handle_interrupt(int port)
{
	int attempt = 0;

	/*
	 * KTU1125's /INT pin is level, so process interrupts until it
	 * deasserts if the chip has a dedicated interrupt pin.
	 */
#ifdef CONFIG_USBC_PPC_DEDICATED_INT
	while (ppc_get_alert_status(port))
#endif
	{
		int ovp_int_count = 0;
		int snk = 0;
		int src = 0;
		int data = 0;

		attempt++;
		if (attempt > 1)
			ppc_prints("Could not clear interrupts on first "
				   "try, retrying",
				   port);

		if (attempt > 10) {
			ppc_prints("Rescheduling interrupt handler", port);
			atomic_or(&irq_pending, BIT(port));
			hook_call_deferred(&ktu1125_irq_deferred_data, MSEC);
			return;
		}

		/* Clear the interrupt by reading all 3 registers */
		read_reg(port, KTU1125_INT_SNK, &snk);
		read_reg(port, KTU1125_INT_SRC, &src);
		read_reg(port, KTU1125_INT_DATA, &data);

		CPRINTS("ppc p%d: INTERRUPT snk=%02X src=%02X data=%02X", port,
			snk, src, data);

		if (snk & KTU1125_FR_SWAP)
			pd_got_frs_signal(port);

		if (snk &
		    (KTU1125_SYSA_SCP | KTU1125_SYSA_OCP | KTU1125_VBUS_OVP)) {
			/* Log and PD reset */
			pd_handle_overcurrent(port);
		}

		if (src &
		    (KTU1125_SYSB_CLP | KTU1125_SYSB_OCP | KTU1125_SYSB_SCP |
		     KTU1125_VCONN_CLP | KTU1125_VCONN_SCP)) {
			/* Log and PD reset */
			pd_handle_overcurrent(port);
		}

		if (data & (KTU1125_SBU2_OVP | KTU1125_SBU1_OVP)) {
			/* Log and PD reset */
			pd_handle_overcurrent(port);
		}

		if (data & (KTU1125_CC1_OVP | KTU1125_CC2_OVP)) {
			ppc_prints("CC Over Voltage!", port);
			/*
			 * Bug on ktu1125 Rev A:
			 * OVP interrupts are falsely triggered
			 * after IC reset (RST_L 0-> 1)
			 */
			if (ovp_int_count++)
				pd_handle_cc_overvoltage(port);
		}
	}
}

const struct ppc_drv ktu1125_drv = {
	.init = &ktu1125_init,
	.is_sourcing_vbus = &ktu1125_is_sourcing_vbus,
	.vbus_sink_enable = &ktu1125_vbus_sink_enable,
	.vbus_source_enable = &ktu1125_vbus_source_enable,
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &ktu1125_set_polarity,
#endif
	.set_vbus_source_current_limit = &ktu1125_set_vbus_src_current_limit,
	.discharge_vbus = &ktu1125_discharge_vbus,
#ifdef CONFIG_USBC_PPC_SBU
	.set_sbu = &ktu1125_set_sbu,
#endif
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &ktu1125_set_vconn,
#endif
#ifdef CONFIG_USB_PD_FRS_PPC
	.set_frs_enable = &ktu1125_set_frs_enable,
#endif
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &ktu1125_dump,
#endif
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &ktu1125_is_vbus_present,
#endif
	.interrupt = &ktu1125_interrupt,
};
