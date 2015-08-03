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
#include "ec_commands.h"
#include "gpio.h"
#include "pi3usb9281.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"

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

void usb_charger_task(void)
{
	int port = (task_get_current() == TASK_ID_USB_CHG_P0 ? 0 : 1);

	int device_type, charger_status;
	struct charge_port_info charge;
	int type;

	charge.voltage = USB_CHARGER_VOLTAGE_MV;

	/* Initialize chip and enable interrupts */
	pi3usb9281_init(port);

	while (1) {
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

		/* Wait for interrupt */
		task_wait_event(-1);
	}
}
