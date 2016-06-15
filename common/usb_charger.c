/* Copyright 2015 The Chromium OS Authors. All rights reserved.
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

#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_pd.h"

static void update_vbus_supplier(int port, int vbus_level)
{
	struct charge_port_info charge;

	/*
	 * If VBUS is low, or VBUS is high and we are not outputting VBUS
	 * ourselves, then update the VBUS supplier.
	 */
	if (!vbus_level || !usb_charger_port_is_sourcing_vbus(port)) {
		charge.voltage = USB_CHARGER_VOLTAGE_MV;
		charge.current = vbus_level ? USB_CHARGER_MIN_CURR_MA : 0;
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS,
					     port,
					     &charge);
	}
}

#ifdef CONFIG_USB_PD_5V_EN_ACTIVE_LOW
#define USB_5V_EN(port) !gpio_get_level(GPIO_USB_C##port##_5V_EN_L)
#else
#define USB_5V_EN(port) gpio_get_level(GPIO_USB_C##port##_5V_EN)
#endif

int usb_charger_port_is_sourcing_vbus(int port)
{
	if (port == 0)
		return USB_5V_EN(0);
#if CONFIG_USB_PD_PORT_COUNT >= 2
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

#ifdef HAS_TASK_USB_CHG_P0
	/* USB Charger task(s) */
	task_set_event(USB_CHG_PORT_TO_TASK_ID(port), USB_CHG_EVENT_VBUS, 0);
#endif

#ifdef CONFIG_USB_PD_VBUS_DETECT_CHARGER
	/* USB PD task */
	task_wake(PD_PORT_TO_TASK_ID(port));
#endif
}

static void usb_charger_init(void)
{
	int i;
	struct charge_port_info charge_none;

	/* Initialize all charge suppliers to 0 */
	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_OTHER,
					     i,
					     &charge_none);

#ifndef CONFIG_USB_PD_VBUS_DETECT_TCPC
		/*
		 * Initialize VBUS supplier based on whether VBUS is present.
		 * For CONFIG_USB_PD_VBUS_DETECT_TCPC, usb_charger_vbus_change()
		 * will be called directly from TCPC alert.
		 */
		update_vbus_supplier(i, pd_snk_is_vbus_provided(i));
#endif
	}
}
DECLARE_HOOK(HOOK_INIT, usb_charger_init, HOOK_PRIO_CHARGE_MANAGER_INIT + 1);
