/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager for Nuvoton NCT38XX. */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "nct38xx.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "usb_common.h"

#ifdef CONFIG_ZEPHYR
#include "usbc/tcpc_nct38xx.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_nct38xx.h>
#include <zephyr/drivers/mfd/nct38xx.h>
#endif

#if defined(CONFIG_ZEPHYR) && defined(CONFIG_IO_EXPANDER_NCT38XX)
#error CONFIG_IO_EXPANDER_NCT38XX cannot be used with Zephyr.
#error Enable the Zephyr driver CONFIG_GPIO_NCT38XX instead.
#endif

/*
 * TODO(b/295587630): nct38xx: upstream gpio_nct38xx_alert.c driver
 * incompatible with downstream TCPC driver
 */
#ifdef CONFIG_GPIO_NCT38XX_ALERT
#error Zephyr driver CONFIG_GPIO_NCT38XX_ALERT cannot be used with the
#error downstream CONFIG_PLATFORM_EC_USB_PD_TCPM_NCT38XX driver.
#error Delete the nuvoton,nct38xx-gpio-alert node from the devicetree.
#endif

#if !defined(CONFIG_USB_PD_TCPM_TCPCI)
#error "NCT38XX is using part of standard TCPCI control"
#error "Please upgrade your board configuration"
#endif

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static enum nct38xx_boot_type boot_type[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_MFD_NCT38XX
static struct k_sem *mfd_lock[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif

test_mockable enum nct38xx_boot_type nct38xx_get_boot_type(int port)
{
	return boot_type[port];
}

test_mockable void nct38xx_reset_notify(int port)
{
	/* A full reset also resets the chip's dead battery boot status */
	boot_type[port] = NCT38XX_BOOT_UNKNOWN;
}

int nct38xx_init(int port)
{
	int rv;
	int reg;

#ifdef CONFIG_MFD_NCT38XX
	if (!device_is_ready(tcpc_config[port].mfd_parent)) {
		return EC_ERROR_INVALID_CONFIG;
	}

	mfd_lock[port] =
		mfd_nct38xx_get_lock_reference(tcpc_config[port].mfd_parent);
#endif

	/*
	 * Detect dead battery boot by the default role control value of 0x0A
	 * once per EC run
	 */
	if (boot_type[port] == NCT38XX_BOOT_UNKNOWN) {
		RETURN_ERROR(tcpc_read(port, TCPC_REG_ROLE_CTRL, &reg));

		if (reg == NCT38XX_ROLE_CTRL_DEAD_BATTERY)
			boot_type[port] = NCT38XX_BOOT_DEAD_BATTERY;
		else
			boot_type[port] = NCT38XX_BOOT_NORMAL;
	}

	RETURN_ERROR(tcpc_read(port, TCPC_REG_POWER_STATUS, &reg));

	/*
	 * Set TCPC_CONTROL.DebugAccessoryControl = 1 to control by TCPM,
	 * not TCPC in most cases.  This must be left alone if we're on a
	 * dead battery boot with a debug accessory.  CC line detection will
	 * be delayed if we have booted from a dead battery with a debug
	 * accessory and change this bit (see b/186799392).
	 */
	if ((boot_type[port] == NCT38XX_BOOT_DEAD_BATTERY) &&
	    (reg & TCPC_REG_POWER_STATUS_DEBUG_ACC_CON))
		CPRINTS("C%d: Booted in dead battery mode, not changing debug"
			" control",
			port);
	else if (tcpc_config[port].flags & TCPC_FLAGS_NO_DEBUG_ACC_CONTROL)
		CPRINTS("C%d: NO_DEBUG_ACC_CONTROL", port);
	else {
		RETURN_ERROR(tcpc_update8(port, TCPC_REG_TCPC_CTRL,
					  TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL,
					  MASK_SET));
	}

	/*
	 * Write to the CONTROL_OUT_EN register to enable:
	 * [6] - CONNDIREN : Connector direction indication output enable
	 * [2] - SNKEN     : VBUS sink enable output enable
	 * [0] - SRCEN     : VBUS source voltage enable output enable
	 */
	reg = NCT38XX_REG_CTRL_OUT_EN_SRCEN | NCT38XX_REG_CTRL_OUT_EN_SNKEN |
	      NCT38XX_REG_CTRL_OUT_EN_CONNDIREN;

	rv = tcpc_write(port, NCT38XX_REG_CTRL_OUT_EN, reg);
	if (rv)
		return rv;

	/* Disable OVP */
	rv = tcpc_update8(port, TCPC_REG_FAULT_CTRL,
			  TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS, MASK_SET);
	if (rv)
		return rv;

	/* Enable VBus monitor and Disable FRS */
	rv = tcpc_update8(port, TCPC_REG_POWER_CTRL,
			  (TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
			   TCPC_REG_POWER_CTRL_FRS_ENABLE),
			  MASK_CLR);
	if (rv)
		return rv;

	/* Set FRS direction for SNK detect, if FRS is enabled */
	if (tcpm_tcpc_has_frs_control(port)) {
		reg = TCPC_REG_CONFIG_EXT_1_FR_SWAP_SNK_DIR;
		rv = tcpc_write(port, TCPC_REG_CONFIG_EXT_1, reg);
		if (rv)
			return rv;
	}

	/* Start VBus monitor */
	rv = tcpc_write(port, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_ENABLE_VBUS_DETECT);
	if (rv)
		return rv;

	/**
	 * Set driver specific ALERT mask bits
	 *
	 * Wake up on faults
	 */
	reg = TCPC_REG_ALERT_FAULT;

	/*
	 * Enable the Vendor Define alert event only when the IO expander
	 * feature is defined
	 */
	if (IS_ENABLED(CONFIG_IO_EXPANDER_NCT38XX) ||
	    IS_ENABLED(CONFIG_GPIO_NCT38XX)) {
#ifdef CONFIG_ZEPHYR
		const struct device *dev =
			nct38xx_get_gpio_device_from_port(port);

		if (!device_is_ready(dev)) {
			CPRINTS("device %s not ready", dev->name);
			return EC_ERROR_BUSY;
		}
#endif /* CONFIG_ZEPHYR */
		reg |= TCPC_REG_ALERT_VENDOR_DEF;
	}

	rv = tcpc_update16(port, TCPC_REG_ALERT_MASK, reg, MASK_SET);

	if (rv)
		return rv;

	/* Enable full VCONN protection (Over-Current and Short-Circuit) */
	reg = NCT38XX_REG_VBC_FAULT_CTL_VC_OCP_EN |
	      NCT38XX_REG_VBC_FAULT_CTL_VC_SCP_EN |
	      NCT38XX_REG_VBC_FAULT_CTL_FAULT_VC_OFF;

	rv = tcpc_update8(port, NCT38XX_REG_VBC_FAULT_CTL, reg, MASK_SET);

	return rv;
}

test_export_static int nct38xx_tcpm_init(int port)
{
	int rv;

	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

	return nct38xx_init(port);
}

test_export_static int nct38xx_tcpm_set_cc(int port, int pull)
{
	/*
	 * Setting the CC lines to open/open requires that the NCT CTRL_OUT
	 * register has sink disabled. Otherwise, when no battery is connected:
	 *
	 * 1. You set CC lines to Open/Open. This is physically happening on
	 *    the CC line.
	 * 2. Since CC is now Open/Open, the internal TCPC HW state machine
	 *    is no longer in Attached.Snk and therefore our TCPC HW
	 *    automatically opens the sink switch (de-assert the VBSNK_EN pin)
	 * 3. Since sink switch is open, the TCPC VCC voltage starts to drop.
	 * 4. When TCPC VCC gets below ~2.7V the TCPC will reset and therefore
	 *    it will present Rd/Rd on the CC lines. Also the VBSNK_EN pin
	 *    after reset is Hi-Z, so the sink switch will get closed again.
	 *
	 * Disabling SNKEN makes the VBSNK_EN pin Hi-Z, so
	 * USB_Cx_TCPC_VBSNK_EN_L will be asserted by external
	 * pull-down, so only do so if already sinking, otherwise
	 * both source and sink switches can be closed, which should
	 * never happen (b/166850036).
	 *
	 * SNKEN will be re-enabled in nct38xx_init above (from tcpm_init), or
	 * when CC lines are set again, or when sinking is disabled.
	 */
	int rv;
	enum mask_update_action action =
		pull == TYPEC_CC_OPEN && tcpm_get_snk_ctrl(port) ? MASK_CLR :
								   MASK_SET;

	rv = tcpc_update8(port, NCT38XX_REG_CTRL_OUT_EN,
			  NCT38XX_REG_CTRL_OUT_EN_SNKEN, action);
	if (rv)
		return rv;

	return tcpci_tcpm_set_cc(port, pull);
}

test_export_static int nct38xx_tcpm_set_snk_ctrl(int port, int enable)
{
	int rv;

	/*
	 * To disable sinking, SNKEN must be enabled so that
	 * USB_Cx_TCPC_VBSNK_EN_L will be driven high.
	 */
	if (!enable) {
		rv = tcpc_update8(port, NCT38XX_REG_CTRL_OUT_EN,
				  NCT38XX_REG_CTRL_OUT_EN_SNKEN, MASK_SET);
		if (rv)
			return rv;
	}

	return tcpci_tcpm_set_snk_ctrl(port, enable);
}

static inline int tcpc_read_alert_no_lpm_exit(int port, int *val)
{
	return tcpc_addr_read16_no_lpm_exit(
		port, tcpc_config[port].i2c_info.addr_flags, TCPC_REG_ALERT,
		val);
}

/* Map Type-C port to IOEX port */
__overridable int board_map_nct38xx_tcpc_port_to_ioex(int port)
{
	return port;
}

static inline void nct38xx_tcpc_vendor_defined_alert(int port)
{
#ifdef CONFIG_ZEPHYR
	const struct device *dev = nct38xx_get_gpio_device_from_port(port);

	nct38xx_gpio_alert_handler(dev);
#else
	int ioexport;

	ioexport = board_map_nct38xx_tcpc_port_to_ioex(port);
	nct38xx_ioex_event_handler(ioexport);
#endif /* CONFIG_ZEPHYR */
}

static void nct38xx_tcpc_alert(int port)
{
	int alert, rv;

	/*
	 * The nct3808 is a dual port chip with a shared ALERT
	 * pin. Avoid taking a port out of LPM if it is not alerting.
	 *
	 * The nct38xx exits Idle mode when ALERT is signaled, so there
	 * is no need to run the TCPM LPM exit code to check the ALERT
	 * register bits (Ref. NCT38n7/8 Datasheet S 2.3.4 "Setting the
	 * I2C to Idle"). In fact, running the TCPM LPM exit code causes
	 * a new CC Status ALERT which has the effect of creating a new
	 * ALERT as a side-effect of handing an ALERT.
	 */
	rv = tcpc_read_alert_no_lpm_exit(port, &alert);
	if (rv == EC_SUCCESS && alert == TCPC_REG_ALERT_NONE) {
		/* No ALERT on this port, return early. */
		return;
	}

	/* Process normal TCPC ALERT event and clear status. */
	tcpci_tcpc_alert(port);

	/*
	 * If the IO expander feature is enabled, use the ALERT register
	 * value read before it was cleared by calling
	 * tcpci_tcpc_alert().  Check the Vendor Defined Alert bit to
	 * handle the IOEX IO's interrupt event.
	 */
	if ((IS_ENABLED(CONFIG_IO_EXPANDER_NCT38XX) ||
	     IS_ENABLED(CONFIG_GPIO_NCT38XX)) &&
	    rv == EC_SUCCESS && (alert & TCPC_REG_ALERT_VENDOR_DEF)) {
		nct38xx_tcpc_vendor_defined_alert(port);
	}
}

test_export_static int nct3807_handle_fault(int port, int fault)
{
	int rv = EC_SUCCESS;

	/* Registers are set to default, initialize for our use */
	if (fault & TCPC_REG_FAULT_STATUS_ALL_REGS_RESET) {
		rv = nct38xx_init(port);
	} else {
		/* We don't use TCPC OVP, so just disable it */
		if (fault & TCPC_REG_FAULT_STATUS_VBUS_OVER_VOLTAGE) {
			/* Disable OVP */
			rv = tcpc_update8(
				port, TCPC_REG_FAULT_CTRL,
				TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS,
				MASK_SET);
			if (rv)
				return rv;
		}
		/* Failing AutoDischargeDisconnect should disable it */
		if (fault & TCPC_REG_FAULT_STATUS_AUTO_DISCHARGE_FAIL)
			tcpm_enable_auto_discharge_disconnect(port, 0);
	}
	return rv;
}

__maybe_unused test_export_static int nct38xx_set_frs_enable(int port,
							     int enable)
{
	if (!tcpm_tcpc_has_frs_control(port))
		return EC_SUCCESS;

	/*
	 * From b/192012189: Enabling FRS for this chip should:
	 *
	 * 1.  Make sure that the sink will not disconnect if Vbus will drop
	 * due to the Fast Role Swap by setting VBUS_SINK_DISCONNECT_THRESHOLD
	 * to 0
	 * 2. Enable the FRS interrupt (already done in TCPCI alert init)
	 * 3. Set POWER_CONTORL.FastRoleSwapEnable to 1
	 */
	RETURN_ERROR(tcpc_write16(
		port, TCPC_REG_VBUS_SINK_DISCONNECT_THRESH,
		enable ? 0x0000 :
			 TCPC_REG_VBUS_SINK_DISCONNECT_THRESH_DEFAULT));

	return tcpc_update8(port, TCPC_REG_POWER_CTRL,
			    TCPC_REG_POWER_CTRL_FRS_ENABLE,
			    enable ? MASK_SET : MASK_CLR);
}

#ifdef CONFIG_MFD_NCT38XX
/*
 * The NCT38xx TCPC and NCT38xx GPIO drivers must not access the NC38xx
 * at the same time.  Use the lock provided by the upstream NCT38xx
 * multi-funciton device.
 */
static void nct38xx_lock(int port, int lock)
{
	if (lock) {
		k_sem_take(mfd_lock[port], K_FOREVER);
	} else {
		k_sem_give(mfd_lock[port]);
	}
}
#endif

const struct tcpm_drv nct38xx_tcpm_drv = {
	.init = &nct38xx_tcpm_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.get_vbus_voltage = &tcpci_get_vbus_voltage,
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &nct38xx_tcpm_set_cc,
	.set_polarity = &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &tcpci_tcpm_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &nct38xx_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
		&tcpci_tcpc_enable_auto_discharge_disconnect,
	.debug_accessory = &tcpci_tcpc_debug_accessory,

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
	.get_snk_ctrl = &tcpci_tcpm_get_snk_ctrl,
	.set_snk_ctrl = &nct38xx_tcpm_set_snk_ctrl,
	.get_src_ctrl = &tcpci_tcpm_get_src_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
	.get_chip_info = &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &tcpci_enter_low_power_mode,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
	.get_bist_test_mode = &tcpci_get_bist_test_mode,
#ifdef CONFIG_USB_PD_FRS
	.set_frs_enable = &nct38xx_set_frs_enable,
#endif
	.handle_fault = &nct3807_handle_fault,
	.hard_reset_reinit = &tcpci_hard_reset_reinit,

#ifdef CONFIG_MFD_NCT38XX
	.lock = &nct38xx_lock,
#endif
};
