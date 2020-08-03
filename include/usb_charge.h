/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

#ifndef __CROS_EC_USB_CHARGE_H
#define __CROS_EC_USB_CHARGE_H

#include "charge_manager.h"
#include "common.h"
#include "ec_commands.h"

/* USB charger voltage */
#define USB_CHARGER_VOLTAGE_MV  5000
/* USB charger minimum current */
#define USB_CHARGER_MIN_CURR_MA 500
/*
 * USB charger maximum current
 *
 * The USB Type-C specification limits the maximum amount of current from BC 1.2
 * suppliers to 1.5A.  Technically, proprietary methods are not allowed, but we
 * will continue to allow those.
 */
#define USB_CHARGER_MAX_CURR_MA 1500

#define USB_SYSJUMP_TAG 0x5550 /* "UP" - Usb Port */
#define USB_HOOK_VERSION 1

#ifdef CONFIG_USB_PORT_POWER_SMART
#define USB_PORT_ENABLE_COUNT CONFIG_USB_PORT_POWER_SMART_PORT_COUNT
#elif defined(CONFIG_USB_PORT_POWER_DUMB)
#define USB_PORT_ENABLE_COUNT USB_PORT_COUNT
#endif

/* GPIOs to enable/disable USB ports. Board specific. */
#ifdef USB_PORT_ENABLE_COUNT
#ifdef CONFIG_USB_PORT_ENABLE_DYNAMIC
extern int usb_port_enable[USB_PORT_ENABLE_COUNT];
#else
extern const int usb_port_enable[USB_PORT_ENABLE_COUNT];
#endif
#endif /* USB_PORT_ENABLE_COUNT */

/**
 * Set USB charge mode for the port.
 *
 * @param usb_port_id		Port to set.
 * @param mode			New mode for port.
 * @param inhibit_charge	Inhibit charging during system suspend.
 * @return EC_SUCCESS, or non-zero if error.
 */
int usb_charge_set_mode(int usb_port_id, enum usb_charge_mode mode,
			enum usb_suspend_charge inhibit_charge);

#define USB_CHG_EVENT_BC12	TASK_EVENT_CUSTOM_BIT(0)
#define USB_CHG_EVENT_VBUS	TASK_EVENT_CUSTOM_BIT(1)
#define USB_CHG_EVENT_INTR	TASK_EVENT_CUSTOM_BIT(2)
#define USB_CHG_EVENT_DR_UFP	TASK_EVENT_CUSTOM_BIT(3)
#define USB_CHG_EVENT_DR_DFP	TASK_EVENT_CUSTOM_BIT(4)
#define USB_CHG_EVENT_CC_OPEN	TASK_EVENT_CUSTOM_BIT(5)
#define USB_CHG_EVENT_MUX	TASK_EVENT_CUSTOM_BIT(6)

/* Number of USB_CHG_* tasks */
#ifdef HAS_TASK_USB_CHG_P2
#define USB_CHG_TASK_COUNT 3
#elif defined(HAS_TASK_USB_CHG_P1)
#define USB_CHG_TASK_COUNT 2
#elif defined(HAS_TASK_USB_CHG_P0) || defined(HAS_TASK_USB_CHG)
#define USB_CHG_TASK_COUNT 1
#else
#define USB_CHG_TASK_COUNT 0
#endif

/*
 * Define USB_CHG_PORT_TO_TASK_ID() and TASK_ID_TO_USB_CHG__PORT() macros to
 * go between USB_CHG port number and task ID. Assume that TASK_ID_USB_CHG_P0,
 * is the lowest task ID and IDs are on a continuous range.
 */
#ifdef HAS_TASK_USB_CHG_P0
#define USB_CHG_PORT_TO_TASK_ID(port) (TASK_ID_USB_CHG_P0 + (port))
#define TASK_ID_TO_USB_CHG_PORT(id) ((id) - TASK_ID_USB_CHG_P0)
#else
#define USB_CHG_PORT_TO_TASK_ID(port) -1 /* stub task ID */
#define TASK_ID_TO_USB_CHG_PORT(id) 0
#endif  /* HAS_TASK_USB_CHG_P0 */

/**
 * Returns true if the passed port is a power source.
 *
 * @param port  Port number.
 * @return      True if port is sourcing vbus.
 */
int usb_charger_port_is_sourcing_vbus(int port);

enum usb_switch {
	USB_SWITCH_CONNECT,
	USB_SWITCH_DISCONNECT,
	USB_SWITCH_RESTORE,
};

struct bc12_drv {
	/* All fields below are optional */

	/* BC1.2 detection task for this chip */
	void (*usb_charger_task)(int port);
	/* Configure USB data switches on type-C port */
	void (*set_switches)(int port, enum usb_switch setting);
	/* Check if ramping is allowed for given supplier */
	int (*ramp_allowed)(int supplier);
	/* Get the maximum current limit that we are allowed to ramp to */
	int (*ramp_max)(int supplier, int sup_curr);
};

struct bc12_config {
	const struct bc12_drv *drv;
};
/**
 * An array of length CHARGE_PORT_COUNT which associates each
 * pd port / dedicated charge port to bc12 driver.
 *
 * If CONFIG_BC12_SINGLE_DRIVER is defined, bc12 driver will provide a
 * definition of this array. Otherwise, board should define this by themselves.
 */
extern struct bc12_config bc12_ports[];

/**
 * Configure USB data switches on type-C port.
 *
 * @param port port number.
 * @param setting new switch setting to configure.
 */
static inline void usb_charger_set_switches(int port, enum usb_switch setting)
{
	if (bc12_ports[port].drv->set_switches)
		bc12_ports[port].drv->set_switches(port, setting);
}

/**
 * Notify USB_CHG task that VBUS level has changed.
 *
 * @param port port number.
 * @param vbus_level new VBUS level
 */
void usb_charger_vbus_change(int port, int vbus_level);

/**
 * Check if ramping is allowed for given supplier
 *
 * @param port port number.
 * @supplier Supplier to check
 *
 * @return Ramping is allowed for given supplier
 */
static inline int usb_charger_ramp_allowed(int port, int supplier)
{
	if (port < 0 || !bc12_ports[port].drv->ramp_allowed)
		return 0;
	return bc12_ports[port].drv->ramp_allowed(supplier);
}

/**
 * Get the maximum current limit that we are allowed to ramp to
 *
 * @param port port number.
 * @supplier Active supplier type
 * @sup_curr Input current limit based on supplier
 *
 * @return Maximum current in mA
 */
static inline int usb_charger_ramp_max(int port, int supplier, int sup_curr)
{
	if (port < 0 || !bc12_ports[port].drv->ramp_max)
		return 0;
	return bc12_ports[port].drv->ramp_max(supplier, sup_curr);
}

/**
 * Reset available BC 1.2 chargers on all ports
 * @param port
 */
void usb_charger_reset_charge(int port);

#endif  /* __CROS_EC_USB_CHARGE_H */
