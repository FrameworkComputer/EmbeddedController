/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB charger interface routines. This code assumes that CONFIG_CHARGE_MANAGER
 * is defined and implemented.
 * usb_charger_set_switches() must be implemented by a companion
 * usb_switch driver.
 * In addition,  USB switch-specific usb_charger task or interrupt routine
 * is necessary to update charge_manager with detected charger attributes.
 */

#include "builtin/assert.h"
#include "charge_manager.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "stddef.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_flags.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK

/* Hold the event bits for all ports, 1 byte per port. */
static atomic_t usb_charger_port_events;

/* Convert event bits for port so it can be stored in a 32 bit value. */
#define PORT_EVENT_PACK(port, event) ((event & 0xff) << (8 * port))

/* Extract the event bits for port from a 32 bit value. */
#define PORT_EVENT_UNPACK(port, event_packed) \
	((event_packed >> (8 * port)) & 0xff)

/* Ensure port event bits are valid. */
BUILD_ASSERT(BIT(0) == TASK_EVENT_CUSTOM_BIT(0));
BUILD_ASSERT(BIT(1) == TASK_EVENT_CUSTOM_BIT(1));
BUILD_ASSERT(BIT(2) == TASK_EVENT_CUSTOM_BIT(2));
BUILD_ASSERT(BIT(3) == TASK_EVENT_CUSTOM_BIT(3));

#endif /* CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK */

static void update_vbus_supplier(int port, int vbus_level)
{
	struct charge_port_info charge = { 0 };

	if (vbus_level && !usb_charger_port_is_sourcing_vbus(port)) {
		charge.voltage = USB_CHARGER_VOLTAGE_MV;
		charge.current = USB_CHARGER_MIN_CURR_MA;
	}

	charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, port, &charge);
}

#ifdef CONFIG_USB_PD_5V_EN_CUSTOM
#define USB_5V_EN(port) board_is_sourcing_vbus(port)
#elif defined(CONFIG_USBC_PPC)
#define USB_5V_EN(port) ppc_is_sourcing_vbus(port)
#elif defined(CONFIG_USB_PD_PPC)
#define USB_5V_EN(port) tcpci_tcpm_get_src_ctrl(port)
#elif defined(CONFIG_USB_PD_5V_CHARGER_CTRL)
#define USB_5V_EN(port) charger_is_sourcing_otg_power(port)
#elif defined(CONFIG_USB_PD_5V_EN_ACTIVE_LOW)
#define USB_5V_EN(port) !gpio_get_level(GPIO_USB_C##port##_5V_EN_L)
#else
#define USB_5V_EN(port) gpio_get_level(GPIO_USB_C##port##_5V_EN)
#endif

int usb_charger_port_is_sourcing_vbus(int port)
{
	if (port == 0)
		return USB_5V_EN(0);
#if CONFIG_USB_PD_PORT_MAX_COUNT >= 2
	else if (port == 1)
		return USB_5V_EN(1);
#endif
	/* Not a valid port */
	return 0;
}

void usb_charger_vbus_change(int port, int vbus_level)
{
	/* If VBUS has transitioned low, notify PD module directly */
	if (!vbus_level)
		pd_vbus_low(port);

	/* Update VBUS supplier and signal VBUS change to USB_CHG task */
	update_vbus_supplier(port, vbus_level);

#if defined(HAS_TASK_USB_CHG_P0) || \
	defined(CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK)
	/* USB Charger task(s) */
	usb_charger_task_set_event(port, USB_CHG_EVENT_VBUS);

	/* If we swapped to sourcing, drop any related charge suppliers */
	if (usb_charger_port_is_sourcing_vbus(port))
		usb_charger_reset_charge(port);
#endif

	if ((get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_CHARGER) ||
	    (get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_PPC)) {
		/* USB PD task */
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void usb_charger_reset_charge(int port)
{
	charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY, port, NULL);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP, port, NULL);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP, port, NULL);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP, port, NULL);
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, port, NULL);
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED, port, NULL);
#endif
}

test_mockable void usb_charger_task_set_event(int port, uint8_t event)
{
#ifdef CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK
	atomic_or(&usb_charger_port_events, PORT_EVENT_PACK(port, event));
	task_set_event(TASK_ID_USB_CHG, BIT(port));
#else
	task_set_event(USB_CHG_PORT_TO_TASK_ID(port), event);
#endif
}

void usb_charger_task_set_event_sync(int port, uint8_t event)
{
	struct bc12_config *bc12_port;

	bc12_port = &bc12_ports[port];

	bc12_port->drv->usb_charger_task_event(port, event);
}

static void usb_charger_init(void)
{
	int i;
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		usb_charger_reset_charge(i);
		/* Initialize VBUS supplier based on whether VBUS is present. */
		update_vbus_supplier(i, pd_is_vbus_present(i));
	}
}
DECLARE_HOOK(HOOK_INIT, usb_charger_init, HOOK_PRIO_POST_CHARGE_MANAGER);

__overridable bool board_usb_charger_support(void)

{
	return true;
}

#ifdef CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK
void usb_charger_task_shared(void *u)
{
	int port;
	uint32_t evt;
	uint32_t port_evt;
	struct bc12_config *bc12_port;

	if (!board_usb_charger_support())
		return;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		bc12_port = &bc12_ports[port];

		ASSERT(bc12_port->drv->usb_charger_task_init);
		ASSERT(bc12_port->drv->usb_charger_task_event);

		bc12_port->drv->usb_charger_task_init(port);
	}

	while (1) {
		evt = task_wait_event(-1);

		for (port = 0; port < board_get_usb_pd_port_count(); port++) {
			if (!(evt & BIT(port))) {
				continue;
			}

			port_evt = PORT_EVENT_UNPACK(
				port, atomic_get(&usb_charger_port_events));
			atomic_and(&usb_charger_port_events,
				   ~PORT_EVENT_PACK(port, port_evt));

			usb_charger_task_set_event_sync(port, port_evt);
		}
	}
}

#else /* CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK */

void usb_charger_task(void *u)
{
	int port = TASK_ID_TO_USB_CHG_PORT(task_get_current());
	uint32_t evt;
	struct bc12_config *bc12_port;

	if (!board_usb_charger_support())
		return;

	/*
	 * The actual number of ports may be less than the maximum
	 * configured, so only run the task if the port exists.
	 */
	if (port >= board_get_usb_pd_port_count())
		return;

	bc12_port = &bc12_ports[port];

	ASSERT(bc12_port->drv->usb_charger_task_init);
	ASSERT(bc12_port->drv->usb_charger_task_event);

	bc12_port->drv->usb_charger_task_init(port);

	while (1) {
		evt = task_wait_event(-1);

		usb_charger_task_set_event_sync(port, evt);
	}
}

#endif /* CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK */
