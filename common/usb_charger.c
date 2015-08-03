/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB charger / BC1.2 task. This code assumes that CONFIG_CHARGE_MANAGER
 * is defined and implemented. PI3USB9281 is the only charger detector
 * currently supported.
 */

#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "pi3usb9281.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Wait after a charger is detected to debounce pin contact order */
#define USB_CHG_DEBOUNCE_DELAY_MS 1000
/*
 * Wait after reset, before re-enabling attach interrupt, so that the
 * spurious attach interrupt from certain ports is ignored.
 */
#define USB_CHG_RESET_DELAY_MS 100

/*
 * Store the state of our USB data switches so that they can be restored
 * after pericom reset.
 */
static int usb_switch_state[CONFIG_USB_PD_PORT_COUNT];
static struct mutex usb_switch_lock[CONFIG_USB_PD_PORT_COUNT];

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

int usb_charger_port_is_sourcing_vbus(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_5V_EN);
#if CONFIG_USB_PD_PORT_COUNT >= 2
	else if (port == 1)
		return gpio_get_level(GPIO_USB_C1_5V_EN);
#endif
	/* Not a valid port */
	return 0;
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
	/* If switch is not changing then return */
	if (setting == usb_switch_state[port])
		return;

	mutex_lock(&usb_switch_lock[port]);
	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state[port] = setting;
	pi3usb9281_set_switches(port, usb_switch_state[port]);
	mutex_unlock(&usb_switch_lock[port]);
}

void usb_charger_vbus_change(int port, int vbus_level)
{
	/* Update VBUS supplier and signal VBUS change to USB_CHG task */
	update_vbus_supplier(port, vbus_level);
#if CONFIG_USB_PD_PORT_COUNT == 2
	task_set_event(port ? TASK_ID_USB_CHG_P1 : TASK_ID_USB_CHG_P0,
		       USB_CHG_EVENT_VBUS, 0);
#else
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_VBUS, 0);
#endif
}

static void usb_charger_bc12_detect(int port)
{
	int device_type, charger_status;
	struct charge_port_info charge;
	int type;

	charge.voltage = USB_CHARGER_VOLTAGE_MV;

	/* Read interrupt register to clear on chip */
	pi3usb9281_get_interrupts(port);

	if (usb_charger_port_is_sourcing_vbus(port)) {
		/* If we're sourcing VBUS then we're not charging */
		device_type = charger_status = 0;
	} else {
		/* Set device type */
		device_type = pi3usb9281_get_device_type(port);
		charger_status = pi3usb9281_get_charger_status(port);
	}

	/* Debounce pin plug order if we detect a charger */
	if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
		msleep(USB_CHG_DEBOUNCE_DELAY_MS);

		/* next operation might trigger a detach interrupt */
		pi3usb9281_disable_interrupts(port);
		/* Ensure D+/D- are open before resetting */
		pi3usb9281_set_switch_manual(port, 1);
		pi3usb9281_set_pins(port, 0);
		/* Let D+/D- relax to their idle state */
		msleep(40);

		/*
		 * Trigger chip reset to refresh detection registers.
		 * WARNING: This reset is acceptable for samus_pd,
		 * but may not be acceptable for devices that have
		 * an OTG / device mode, as we may be interrupting
		 * the connection.
		 */
		pi3usb9281_reset(port);
		/*
		 * Restore data switch settings - switches return to
		 * closed on reset until restored.
		 */
		usb_charger_set_switches(port, USB_SWITCH_RESTORE);
		/* Clear possible disconnect interrupt */
		pi3usb9281_get_interrupts(port);
		/* Mask attach interrupt */
		pi3usb9281_set_interrupt_mask(port,
					      0xff &
					      ~PI3USB9281_INT_ATTACH);
		/* Re-enable interrupts */
		pi3usb9281_enable_interrupts(port);
		msleep(USB_CHG_RESET_DELAY_MS);

		/* Clear possible attach interrupt */
		pi3usb9281_get_interrupts(port);
		/* Re-enable attach interrupt */
		pi3usb9281_set_interrupt_mask(port, 0xff);

		/* Re-read ID registers */
		device_type = pi3usb9281_get_device_type(port);
		charger_status = pi3usb9281_get_charger_status(port);
	}

	/* Attachment: decode + update available charge */
	if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
		if (PI3USB9281_CHG_STATUS_ANY(charger_status))
			type = CHARGE_SUPPLIER_PROPRIETARY;
		else if (device_type & PI3USB9281_TYPE_CDP)
			type = CHARGE_SUPPLIER_BC12_CDP;
		else if (device_type & PI3USB9281_TYPE_DCP)
			type = CHARGE_SUPPLIER_BC12_DCP;
		else if (device_type & PI3USB9281_TYPE_SDP)
			type = CHARGE_SUPPLIER_BC12_SDP;
		else
			type = CHARGE_SUPPLIER_OTHER;

		charge.current = pi3usb9281_get_ilim(device_type,
						     charger_status);
		charge_manager_update_charge(type, port, &charge);
	} else { /* Detachment: update available charge to 0 */
		charge.current = 0;
		charge_manager_update_charge(
					CHARGE_SUPPLIER_PROPRIETARY,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_BC12_CDP,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_BC12_DCP,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_BC12_SDP,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_OTHER,
					port,
					&charge);
	}

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

void usb_charger_task(void)
{
	int port = (task_get_current() == TASK_ID_USB_CHG_P0 ? 0 : 1);
	uint32_t evt;

	/* Initialize chip and enable interrupts */
	pi3usb9281_init(port);

	usb_charger_bc12_detect(port);

	while (1) {
		/* Wait for interrupt */
		evt = task_wait_event(-1);

		/* Interrupt from the Pericom chip, determine charger type */
		if (evt & USB_CHG_EVENT_BC12)
			usb_charger_bc12_detect(port);

		/*
		 * Re-enable interrupts on pericom charger detector since the
		 * chip may periodically reset itself, and come back up with
		 * registers in default state. TODO(crosbug.com/p/33823): Fix
		 * these unwanted resets.
		 */
		if (evt & USB_CHG_EVENT_VBUS) {
			pi3usb9281_enable_interrupts(port);
#ifndef CONFIG_USB_PD_TCPM_VBUS
			CPRINTS("VBUS p%d %d", port,
				pd_snk_is_vbus_provided(port));
#endif
		}
	}
}

static void usb_charger_init(void)
{
	int i;
	struct charge_port_info charge_none;

	/* Initialize all pericom charge suppliers to 0 */
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

#ifndef CONFIG_USB_PD_TCPM_VBUS
		/* Initialize VBUS supplier based on whether VBUS is present */
		update_vbus_supplier(i, pd_snk_is_vbus_provided(i));
#endif
	}
}
DECLARE_HOOK(HOOK_INIT, usb_charger_init, HOOK_PRIO_DEFAULT);
