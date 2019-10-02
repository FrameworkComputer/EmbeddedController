/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NX20P348x USB-C Power Path Controller */

#include "common.h"
#include "console.h"
#include "driver/ppc/nx20p348x.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "tcpm.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint32_t irq_pending; /* Bitmask of ports signaling an interrupt. */

#define NX20P348X_DB_EXIT_FAIL_THRESHOLD 10
static int db_exit_fail_count[CONFIG_USB_PD_PORT_MAX_COUNT];

#define NX20P348X_FLAGS_SOURCE_ENABLED BIT(0)
static uint8_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

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

static int nx20p348x_set_ovp_limit(int port)
{
	int rv;
	int reg;

	/* Set VBUS over voltage threshold (OVLO) */
	rv = read_reg(port, NX20P348X_OVLO_THRESHOLD_REG, &reg);
	if (rv)
		return rv;
	/* OVLO threshold is 3 bit field */
	reg &= ~NX20P348X_OVLO_THRESHOLD_MASK;
	/* Set SNK OVP to 23.0 V */
	reg |= NX20P348X_OVLO_23_0;
	rv = write_reg(port, NX20P348X_OVLO_THRESHOLD_REG, reg);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

static int nx20p348x_is_sourcing_vbus(int port)
{
	return flags[port] & NX20P348X_FLAGS_SOURCE_ENABLED;
}

static int nx20p348x_set_vbus_source_current_limit(int port,
						 enum tcpc_rp_value rp)
{
	int regval;
	int status;

	status = read_reg(port, NX20P348X_5V_SRC_OCP_THRESHOLD_REG, &regval);
	if (status)
		return status;

	regval &= ~NX20P348X_ILIM_MASK;

	/* We need buffer room for all current values. */
	switch (rp) {
	case TYPEC_RP_3A0:
		regval |= NX20P348X_ILIM_3_200;
		break;

	case TYPEC_RP_1A5:
		regval |= NX20P348X_ILIM_1_600;
		break;

	case TYPEC_RP_USB:
	default:
		regval |= NX20P348X_ILIM_0_600;
		break;
	};


	return write_reg(port, NX20P348X_5V_SRC_OCP_THRESHOLD_REG, regval);
}

static int nx20p348x_discharge_vbus(int port, int enable)
{
	int regval;
	int status;

	status = read_reg(port, NX20P348X_DEVICE_CONTROL_REG, &regval);
	if (status)
		return status;

	if (enable)
		regval |= NX20P348X_CTRL_VBUSDIS_EN;
	else
		regval &= ~NX20P348X_CTRL_VBUSDIS_EN;

	status = write_reg(port, NX20P348X_DEVICE_CONTROL_REG, regval);
	if (status) {
		CPRINTS("Failed to %s vbus discharge",
			enable ? "enable" : "disable");
		return status;
	}

	return EC_SUCCESS;
}

static int nx20p348x_vbus_sink_enable(int port, int enable)
{
	int status;
	int rv;
	int control = enable ? NX20P348X_SWITCH_CONTROL_HVSNK : 0;

	enable = !!enable;

#if defined(CONFIG_USBC_PPC_NX20P3481)
	rv = write_reg(port, NX20P348X_SWITCH_CONTROL_REG, control);
#elif defined(CONFIG_USBC_PPC_NX20P3483)
	/*
	 * We cannot use an EC GPIO for EN_SNK since an EC reset will float the
	 * GPIO thus browning out the board (without a battery).
	 */
	rv = tcpm_set_snk_ctrl(port, enable);
#else
#error "Either the NX20P3481 or NX20P3483 must be selected"
#endif
	if (rv)
		return rv;

	/*
	 * Read switch status register. The bit definitions for switch control
	 * and switch status resister are identical, so the control value can be
	 * compared against the status value. The control switch has a debounce
	 * (15 msec) before the status will reflect the control command.
	 */
	msleep(NX20P348X_SWITCH_STATUS_DEBOUNCE_MSEC);
	rv = read_reg(port, NX20P348X_SWITCH_STATUS_REG, &status);
	if (rv)
		return rv;

	return (status & NX20P348X_SWITCH_STATUS_MASK) == control ?
		EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static int nx20p348x_vbus_source_enable(int port, int enable)
{
	int status;
	int rv;
	uint8_t previous_flags = flags[port];
	int control = enable ? NX20P348X_SWITCH_CONTROL_5VSRC : 0;

	enable = !!enable;

#if defined(CONFIG_USBC_PPC_NX20P3481)
	rv = write_reg(port, NX20P348X_SWITCH_CONTROL_REG, control);
#elif defined(CONFIG_USBC_PPC_NX20P3483)
	/*
	 * For parity's sake, we should not use an EC GPIO for EN_SRC since we
	 * cannot use it for EN_SNK (for brown out reason listed above).
	 */
	rv = tcpm_set_src_ctrl(port, enable);
#else
#error "Either the NX20P3481 or NX20P3483 must be selected"
#endif
	if (rv)
		return rv;

	/* Cache the anticipated Vbus state */
	if (enable)
		flags[port] |= NX20P348X_FLAGS_SOURCE_ENABLED;
	else
		flags[port] &= ~NX20P348X_FLAGS_SOURCE_ENABLED;

	/*
	 * Read switch status register. The bit definitions for switch control
	 * and switch status resister are identical, so the control value can be
	 * compared against the status value. The control switch has a debounce
	 * (15 msec) before the status will reflect the control command.
	 */
	msleep(NX20P348X_SWITCH_STATUS_DEBOUNCE_MSEC);
	rv = read_reg(port, NX20P348X_SWITCH_STATUS_REG, &status);
	if (rv) {
		flags[port] = previous_flags;
		return rv;
	}

	if ((status & NX20P348X_SWITCH_STATUS_MASK) != control) {
		flags[port] = previous_flags;
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int nx20p348x_init(int port)
{
	int reg;
	int mask;
	int mode;
	int rv;
	enum tcpc_rp_value initial_current_limit;

	/* Mask interrupts for interrupt 2 register */
	mask = ~NX20P348X_INT2_EN_ERR;
	rv = write_reg(port, NX20P348X_INTERRUPT2_MASK_REG, mask);
	if (rv)
		return rv;

	/* Mask interrupts for interrupt 1 register */
	mask = ~(NX20P348X_INT1_OC_5VSRC | NX20P348X_INT1_SC_5VSRC |
		 NX20P348X_INT1_RCP_5VSRC | NX20P348X_INT1_DBEXIT_ERR);
#if defined(CONFIG_USBC_PPC_NX20P3481) || defined(CONFIG_USBC_PPC_NX20P3483)
	/* Unmask Fast Role Swap detect interrupt */
	mask &= ~NX20P348X_INT1_FRS_DET;
#endif
	rv = write_reg(port, NX20P348X_INTERRUPT1_MASK_REG, mask);
	if (rv)
		return rv;

	/* Clear any pending interrupts by reading interrupt registers */
	read_reg(port, NX20P348X_INTERRUPT1_REG, &reg);
	read_reg(port, NX20P348X_INTERRUPT2_REG, &reg);

	/* Get device  mode */
	rv = read_reg(port, NX20P348X_DEVICE_STATUS_REG, &mode);
	if (rv)
		return rv;
	mode &= NX20P348X_DEVICE_MODE_MASK;

	/* Check if dead battery mode is active. */
	if (mode == NX20P348X_MODE_DEAD_BATTERY) {
		/*
		 * If in dead battery mode, must enable HV SNK mode prior to
		 * exiting dead battery mode or VBUS path will get cut off and
		 * system will lose power. Before DB mode is exited, the device
		 * mode will not reflect the correct value and therefore the
		 * return value isn't useful here.
		 */
		nx20p348x_vbus_sink_enable(port, 1);

		/* Exit dead battery mode. */
		rv = read_reg(port, NX20P348X_DEVICE_CONTROL_REG, &reg);
		if (rv)
			return rv;
		reg |= NX20P348X_CTRL_DB_EXIT;
		rv = write_reg(port, NX20P348X_DEVICE_CONTROL_REG, reg);
		if (rv)
			return rv;
	}

	/*
	 * Set VBUS over voltage threshold (OVLO). Note that while the PPC is in
	 * dead battery mode, OVLO is forced to 6.8V, so this setting must be
	 * done after dead battery mode is exited.
	 */
	nx20p348x_set_ovp_limit(port);

	/* Set the Vbus current limit after dead battery mode exit */
#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	initial_current_limit = CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;
#else
	initial_current_limit = TYPEC_RP_1A5;
#endif
	nx20p348x_set_vbus_source_current_limit(port, initial_current_limit);

	return EC_SUCCESS;
}

static void nx20p348x_handle_interrupt(int port)
{
	int reg;
	int control_reg;

	/*
	 * Read interrupt 1 status register. Note, interrupt register is
	 * automatically cleared by reading.
	 */
	read_reg(port, NX20P348X_INTERRUPT1_REG, &reg);

	/* Check for DBEXIT error */
	if (reg & NX20P348X_INT1_DBEXIT_ERR) {
		int mask_reg;

		/*
		 * This failure is not expected. If for some reason, this
		 * keeps happening, then log an error and mask the interrupt to
		 * prevent interrupt floods.
		 */
		if (++db_exit_fail_count[port] >=
		    NX20P348X_DB_EXIT_FAIL_THRESHOLD) {
			CPRINTS("Port %d PPC failed to exit DB mode", port);
			if (read_reg(port, NX20P348X_INTERRUPT1_MASK_REG,
				    &mask_reg)) {
				mask_reg |= NX20P348X_INT1_DBEXIT_ERR;
				write_reg(port, NX20P348X_INTERRUPT1_MASK_REG,
					  mask_reg);
			}
		}
		read_reg(port, NX20P348X_DEVICE_CONTROL_REG, &control_reg);
		reg |= NX20P348X_CTRL_DB_EXIT;
		write_reg(port, NX20P348X_DEVICE_CONTROL_REG, control_reg);
		/*
		 * If DB exit mode failed, then the OVP limit setting done in
		 * the init routine will not be successful. Set the OVP limit
		 * again here.
		 */
		nx20p348x_set_ovp_limit(port);
	}

	/* Check for 5V OC interrupt */
	if (reg & NX20P348X_INT1_OC_5VSRC) {
		CPRINTS("C%d: PPC detected Vbus overcurrent!", port);
		pd_handle_overcurrent(port);
	}

	/* Check for Vbus reverse current protection */
	if (reg & NX20P348X_INT1_RCP_5VSRC)
		CPRINTS("C%d: PPC detected Vbus reverse current!", port);

	/* Check for Vbus short protection */
	if (reg & NX20P348X_INT1_SC_5VSRC)
		CPRINTS("C%d: PPC Vbus short detected!", port);

#ifdef CONFIG_USBC_PPC_NX20P3481
	/* Check for FRS detection */
	if (reg & NX20P348X_INT1_FRS_DET) {
		/*
		 * TODO(b/113069469): Need to check for CC status and verifiy
		 * that a sink is attached to continue with FRS. If a sink is
		 * not attached, then this FRS detect is a false detect which is
		 * triggered when removing an external charger. If FRS was
		 * detected by the PPC, then it has automatically  enabled the
		 * 5V SRC mode and this must be undone for a proper detach.
		 */
		/* Check CC status */

		/*
		 * False detect, disable SRC mode which was enabled by
		 * NX20P3481.
		 */
		CPRINTS("C%d: PPC FRS false detect, disabling SRC mode!", port);
		nx20p348x_vbus_source_enable(port, 0);
	}
#endif

	/*
	 * Read interrupt 2 status register. Note, interrupt register is
	 * automatically cleared by reading.
	 */
	/*
	 * TODO (b/75272421): Not sure if any of these interrupts
	 * will be used. Might want to use EN_ERR which tracks when both
	 * SNK_EN and SRC_EN are set. However, since for the Analogix TCPC
	 * these values aren't controlled by the EC directly, not sure what
	 * action if any can be taken.
	 */
	read_reg(port, NX20P348X_INTERRUPT2_REG, &reg);
}

static void nx20p348x_irq_deferred(void)
{
	int i;
	uint32_t pending = atomic_read_clear(&irq_pending);

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		if (BIT(i) & pending)
			nx20p348x_handle_interrupt(i);
}
DECLARE_DEFERRED(nx20p348x_irq_deferred);

void nx20p348x_interrupt(int port)
{
	atomic_or(&irq_pending, BIT(port));
	hook_call_deferred(&nx20p348x_irq_deferred_data, 0);
}

#ifdef CONFIG_CMD_PPC_DUMP
static int nx20p348x_dump(int port)
{
	int reg_addr;
	int reg;
	int rv;

	ccprintf("Port %d NX20P348X registers\n", port);
	for (reg_addr = NX20P348X_DEVICE_ID_REG; reg_addr <=
		     NX20P348X_DEVICE_CONTROL_REG; reg_addr++) {
		rv = read_reg(port, reg_addr, &reg);
		if (rv) {
			ccprintf("nx20p: Failed to read register 0x%x\n",
				 reg_addr);
			return rv;
		}
		ccprintf("[0x%02x]: 0x%02x\n", reg_addr, reg);

		/* Flush every call otherwise buffer may get full */
		cflush();
	}

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

/*
 * TODO (b/112697473): The NX20P348x PPCs do not support vbus detection or vconn
 * generation. However, if a different PPC does support these features and needs
 * these config options, then these functions do need to exist. The
 * configuration for what each PPC supports should be converted to bits within
 * a flags variable that is part of the ppc_config_t struct.
 */
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int nx20p348x_is_vbus_present(int port)
{
	return EC_ERROR_UNIMPLEMENTED;
}
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */

#ifdef CONFIG_USBC_PPC_POLARITY
static int nx20p348x_set_polarity(int port, int polarity)
{
	return EC_ERROR_UNIMPLEMENTED;
}
#endif

#ifdef CONFIG_USBC_PPC_VCONN
static int nx20p348x_set_vconn(int port, int enable)
{
	return EC_ERROR_UNIMPLEMENTED;
}
#endif

const struct ppc_drv nx20p348x_drv = {
	.init = &nx20p348x_init,
	.is_sourcing_vbus = &nx20p348x_is_sourcing_vbus,
	.vbus_sink_enable = &nx20p348x_vbus_sink_enable,
	.vbus_source_enable = &nx20p348x_vbus_source_enable,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &nx20p348x_dump,
#endif /* defined(CONFIG_CMD_PPC_DUMP) */
	.set_vbus_source_current_limit =
		&nx20p348x_set_vbus_source_current_limit,
	.discharge_vbus = &nx20p348x_discharge_vbus,
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &nx20p348x_is_vbus_present,
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &nx20p348x_set_polarity,
#endif /* defined(CONFIG_USBC_PPC_POLARITY) */
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &nx20p348x_set_vconn,
#endif /* defined(CONFIG_USBC_PPC_VCONN) */
};
