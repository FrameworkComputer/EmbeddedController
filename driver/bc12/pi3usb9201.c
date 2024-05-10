/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PI3USB9201 USB BC 1.2 Charger Detector driver. */

#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "pi3usb9201.h"
#include "power.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

enum pi3usb9201_client_sts {
	CHG_OTHER = 0,
	CHG_2_4A,
	CHG_2_0A,
	CHG_1_0A,
	CHG_RESERVED,
	CHG_CDP,
	CHG_SDP,
	CHG_DCP,
};

struct bc12_status {
	enum charge_supplier supplier;
	int current_limit;
};

/* Used to store last BC1.2 detection result */
static enum charge_supplier bc12_supplier[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * The USB Type-C specification limits the maximum amount of current from BC 1.2
 * suppliers to 1.5A.  Technically, proprietary methods are not allowed, but we
 * will continue to allow those.
 */
static const struct bc12_status bc12_chg_limits[] = {
	[CHG_OTHER] = { CHARGE_SUPPLIER_OTHER, 500 },
	[CHG_2_4A] = { CHARGE_SUPPLIER_PROPRIETARY, USB_CHARGER_MAX_CURR_MA },
	[CHG_2_0A] = { CHARGE_SUPPLIER_PROPRIETARY, USB_CHARGER_MAX_CURR_MA },
	[CHG_1_0A] = { CHARGE_SUPPLIER_PROPRIETARY, 1000 },
	[CHG_RESERVED] = { CHARGE_SUPPLIER_NONE, 0 },
	[CHG_CDP] = { CHARGE_SUPPLIER_BC12_CDP, USB_CHARGER_MAX_CURR_MA },
	[CHG_SDP] = { CHARGE_SUPPLIER_BC12_SDP, 500 },
	[CHG_DCP] = { CHARGE_SUPPLIER_BC12_DCP, USB_CHARGER_MAX_CURR_MA },
};

static inline int raw_read8(int port, int offset, int *value)
{
	return i2c_read8(pi3usb9201_bc12_chips[port].i2c_port,
			 pi3usb9201_bc12_chips[port].i2c_addr_flags, offset,
			 value);
}

static int pi3usb9201_raw(int port, int reg, int mask, int val)
{
	/* Clear mask and then set val in i2c reg value */
	return i2c_field_update8(pi3usb9201_bc12_chips[port].i2c_port,
				 pi3usb9201_bc12_chips[port].i2c_addr_flags,
				 reg, mask, val);
}

static int pi3usb9201_interrupt_mask(int port, int enable)
{
	return pi3usb9201_raw(port, PI3USB9201_REG_CTRL_1,
			      PI3USB9201_REG_CTRL_1_INT_MASK, enable);
}

static int pi3usb9201_bc12_detect_ctrl(int port, int enable)
{
	return pi3usb9201_raw(port, PI3USB9201_REG_CTRL_2,
			      PI3USB9201_REG_CTRL_2_START_DET,
			      enable ? PI3USB9201_REG_CTRL_2_START_DET : 0);
}

static int pi3usb9201_set_mode(int port, int desired_mode)
{
	return pi3usb9201_raw(port, PI3USB9201_REG_CTRL_1,
			      PI3USB9201_REG_CTRL_1_MODE_MASK,
			      desired_mode << PI3USB9201_REG_CTRL_1_MODE_SHIFT);
}

static __maybe_unused int pi3usb9201_get_mode(int port, int *mode)
{
	int rv;

	rv = raw_read8(port, PI3USB9201_REG_CTRL_1, mode);
	if (rv)
		return rv;

	*mode &= PI3USB9201_REG_CTRL_1_MODE_MASK;
	*mode >>= PI3USB9201_REG_CTRL_1_MODE_SHIFT;

	return EC_SUCCESS;
}

static int pi3usb9201_get_status(int port, int *client, int *host)
{
	int rv;
	int status;

	rv = raw_read8(port, PI3USB9201_REG_CLIENT_STS, &status);
	if (client)
		*client = status;
	rv |= raw_read8(port, PI3USB9201_REG_HOST_STS, &status);
	if (host)
		*host = status;

	return rv;
}

static void bc12_update_supplier(enum charge_supplier supplier, int port,
				 struct charge_port_info *new_chg)
{
	/*
	 * If most recent supplier type is not CHARGE_SUPPLIER_NONE, then the
	 * charge manager table entry for that supplier type needs to be cleared
	 * out.
	 */
	if (bc12_supplier[port] != CHARGE_SUPPLIER_NONE)
		charge_manager_update_charge(bc12_supplier[port], port, NULL);
	/* Now update the current supplier type */
	bc12_supplier[port] = supplier;
	/* If new supplier type != NONE, then notify charge manager */
	if (supplier != CHARGE_SUPPLIER_NONE)
		charge_manager_update_charge(supplier, port, new_chg);
}

static void bc12_update_charge_manager(int port, int client_status)
{
	struct charge_port_info new_chg;
	enum charge_supplier supplier;
	int bit_pos;

	/* Set charge voltage to 5V */
	new_chg.voltage = USB_CHARGER_VOLTAGE_MV;

	/*
	 * Find set bit position. Note that this funciton is only called if a
	 * bit was set in client_status, so bit_pos won't be negative.
	 */
	bit_pos = __builtin_ffs(client_status) - 1;

	new_chg.current = bc12_chg_limits[bit_pos].current_limit;
	supplier = bc12_chg_limits[bit_pos].supplier;

	CPRINTS("pi3usb9201[p%d]: sts = 0x%x, lim = %d mA, supplier = %d", port,
		client_status, new_chg.current, supplier);
	/* bc1.2 is complete and start bit does not auto clear */
	pi3usb9201_bc12_detect_ctrl(port, 0);
	/* Inform charge manager of new supplier type and current limit */
	bc12_update_supplier(supplier, port, &new_chg);
}

static int bc12_detect_start(int port)
{
	int rv;

	/*
	 * Read both status registers to ensure that all interrupt indications
	 * are cleared prior to starting bc1.2 detection.
	 */
	pi3usb9201_get_status(port, NULL, NULL);

	/* Put pi3usb9201 into client mode */
	rv = pi3usb9201_set_mode(port, PI3USB9201_CLIENT_MODE);
	if (rv)
		return rv;
	/* Have pi3usb9201 start bc1.2 detection */
	rv = pi3usb9201_bc12_detect_ctrl(port, 1);
	if (rv)
		return rv;
	/* Unmask interrupt to wake task when detection completes */
	return pi3usb9201_interrupt_mask(port, 0);
}

static void bc12_power_down(int port)
{
	/* Put pi3usb9201 into its power down mode */
	pi3usb9201_set_mode(port, PI3USB9201_POWER_DOWN);
	/* The start bc1.2 bit does not auto clear */
	pi3usb9201_bc12_detect_ctrl(port, 0);
	/* Mask interrupts unitl next bc1.2 detection event */
	pi3usb9201_interrupt_mask(port, 1);
	/*
	 * Let charge manager know there's no more charge available for the
	 * supplier type that was most recently detected.
	 */
	bc12_update_supplier(CHARGE_SUPPLIER_NONE, port, NULL);

	/* There's nothing else to do if the part is always powered. */
	if (pi3usb9201_bc12_chips[port].flags & PI3USB9201_ALWAYS_POWERED)
		return;

#if defined(CONFIG_POWER_PP5000_CONTROL) && defined(CONFIG_AP_POWER_CONTROL)
	/* Indicate PP5000_A rail is not required by USB_CHG task. */
	power_5v_enable(task_get_current(), 0);
#endif
}

static void bc12_power_up(int port)
{
	if (IS_ENABLED(CONFIG_POWER_PP5000_CONTROL) &&
	    IS_ENABLED(CONFIG_AP_POWER_CONTROL) &&
	    !(pi3usb9201_bc12_chips[port].flags & PI3USB9201_ALWAYS_POWERED)) {
		/* Turn on the 5V rail to allow the chip to be powered. */
		power_5v_enable(task_get_current(), 1);
		/*
		 * Give the pi3usb9201 time so it's ready to receive i2c
		 * messages
		 */
		crec_msleep(1);
	}

	pi3usb9201_interrupt_mask(port, 1);
}

static void pi3usb9201_usb_charger_task_init(const int port)
{
	/*
	 * Set most recent bc1.2 detection supplier result to
	 * CHARGE_SUPPLIER_NONE for the port.
	 */
	bc12_supplier[port] = CHARGE_SUPPLIER_NONE;

	/*
	 * The is no specific initialization required for the pi3usb9201 other
	 * than enabling the interrupt mask.
	 */
	pi3usb9201_interrupt_mask(port, 1);
}

static void pi3usb9201_usb_charger_task_event(const int port, uint32_t evt)
{
	/* Interrupt from the Pericom chip, determine charger type */
	if (evt & USB_CHG_EVENT_BC12) {
		int client;
		int host;
		int rv;

		rv = pi3usb9201_get_status(port, &client, &host);
		if (!rv && client)
			/*
			 * Any bit set in client status register indicates that
			 * BC1.2 detection has completed.
			 */
			bc12_update_charge_manager(port, client);
		if (!rv && host) {
#ifdef CONFIG_BC12_CLIENT_MODE_ONLY_PI3USB9201
			pi3usb9201_set_mode(port, PI3USB9201_USB_PATH_ON);
#else
			/*
			 * Switch to SDP after device is plugged in to avoid
			 * noise (pulse on D-) causing USB disconnect
			 * (b/156014140).
			 */
			if (host & PI3USB9201_REG_HOST_STS_DEV_PLUG)
				pi3usb9201_set_mode(port,
						    PI3USB9201_SDP_HOST_MODE);
			/*
			 * Switch to CDP after device is unplugged so we
			 * advertise higher power available for next device.
			 */
			if (host & PI3USB9201_REG_HOST_STS_DEV_UNPLUG)
				pi3usb9201_set_mode(port,
						    PI3USB9201_CDP_HOST_MODE);
#endif
		}
		/*
		 * TODO(b/124061702): Use host status to allocate power more
		 * intelligently.
		 */
	}

	if (!IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_TCPC) &&
	    (evt & USB_CHG_EVENT_VBUS))
		CPRINTS("VBUS p%d %d", port, pd_snk_is_vbus_provided(port));

	if (evt & USB_CHG_EVENT_DR_UFP) {
		bc12_power_up(port);
		if (bc12_detect_start(port)) {
			struct charge_port_info new_chg;

			/*
			 * VBUS is present, but starting bc1.2 detection failed
			 * for some reason. So limit charge current to default
			 * 500 mA for this case.
			 */

			new_chg.voltage = USB_CHARGER_VOLTAGE_MV;
			new_chg.current = USB_CHARGER_MIN_CURR_MA;
			/* Save supplier type and notify chg manager */
			bc12_update_supplier(CHARGE_SUPPLIER_OTHER, port,
					     &new_chg);
			CPRINTS("pi3usb9201[p%d]: bc1.2 failed use defaults",
				port);
		}
	}

	if (evt & USB_CHG_EVENT_DR_DFP) {
#ifdef CONFIG_BC12_CLIENT_MODE_ONLY_PI3USB9201
		pi3usb9201_set_mode(port, PI3USB9201_USB_PATH_ON);
#else
		int mode;
		int rv;

		/*
		 * Update the charge manager if bc1.2 client mode is currently
		 * active.
		 */
		bc12_update_supplier(CHARGE_SUPPLIER_NONE, port, NULL);
		/*
		 * If the port is in DFP mode, then need to set mode to
		 * CDP_HOST which will auto close D+/D- switches.
		 */
		bc12_power_up(port);
		rv = pi3usb9201_get_mode(port, &mode);
		if (!rv && (mode != PI3USB9201_CDP_HOST_MODE)) {
			CPRINTS("pi3usb9201[p%d]: CDP_HOST mode", port);
			/*
			 * Read both status registers to ensure that all
			 * interrupt indications are cleared prior to starting
			 * DFP CDP host mode.
			 */
			pi3usb9201_get_status(port, NULL, NULL);
			pi3usb9201_set_mode(port, PI3USB9201_CDP_HOST_MODE);
			/*
			 * Unmask interrupt to wake task when host status
			 * changes.
			 */
			pi3usb9201_interrupt_mask(port, 0);
		}
#endif
	}

	if (evt & USB_CHG_EVENT_CC_OPEN)
		bc12_power_down(port);
}

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
static int pi3usb9201_ramp_allowed(int supplier)
{
	/* Don't allow ramp if charge supplier is OTHER, SDP, or NONE */
	return !(supplier == CHARGE_SUPPLIER_OTHER ||
		 supplier == CHARGE_SUPPLIER_BC12_SDP ||
		 supplier == CHARGE_SUPPLIER_BC12_DCP ||
		 supplier == CHARGE_SUPPLIER_NONE);
}

static int pi3usb9201_ramp_max(int supplier, int sup_curr)
{
	/*
	 * Use the level from the bc12_chg_limits table above except for
	 * proprietary or CDP and in those cases the charge current from the
	 * charge manager is already set at the max determined by bc1.2
	 * detection.
	 */
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return 500;
	}
}
#endif /* CONFIG_CHARGE_RAMP_SW || CONFIG_CHARGE_RAMP_HW */

const struct bc12_drv pi3usb9201_drv = {
	.usb_charger_task_init = pi3usb9201_usb_charger_task_init,
	.usb_charger_task_event = pi3usb9201_usb_charger_task_event,
#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	.ramp_allowed = pi3usb9201_ramp_allowed,
	.ramp_max = pi3usb9201_ramp_max,
#endif /* CONFIG_CHARGE_RAMP_SW || CONFIG_CHARGE_RAMP_HW */
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config
	bc12_ports[CHARGE_PORT_COUNT] = { [0 ...(CHARGE_PORT_COUNT - 1)] = {
						  .drv = &pi3usb9201_drv,
					  } };
#endif /* CONFIG_BC12_SINGLE_DRIVER */
