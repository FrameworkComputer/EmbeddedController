/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "extpower.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/ps8xxx.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static uint8_t vbus_en[CONFIG_USB_PD_PORT_MAX_COUNT];
static uint8_t vbus_rp[CONFIG_USB_PD_PORT_MAX_COUNT] = { TYPEC_RP_1A5,
							 TYPEC_RP_1A5 };

int board_vbus_source_enabled(int port)
{
	return vbus_en[port];
}

static void board_vbus_update_source_current(int port)
{
	enum gpio_signal gpio_5v_en = port ? GPIO_USB_C1_5V_EN :
					     GPIO_USB_C0_5V_EN;
	enum gpio_signal gpio_3a_en = port ? GPIO_USB_C1_3A_EN :
					     GPIO_USB_C0_3A_EN;

	if (system_get_board_version() >= 1) {
		/*
		 * For rev1 and beyond, 1.5 vs 3.0 A limit is controlled by a
		 * dedicated gpio where high = 3.0A and low = 1.5A. VBUS on/off
		 * is controlled by GPIO_USB_C0/1_5V_EN. Both of these signals
		 * can remain outputs.
		 */
		gpio_set_level(gpio_3a_en,
			       vbus_rp[port] == TYPEC_RP_3A0 ? 1 : 0);
		gpio_set_level(gpio_5v_en, vbus_en[port]);
	} else {
		/*
		 * Driving USB_Cx_5V_EN high, actually put a 16.5k resistance
		 * (2x 33k in parallel) on the NX5P3290 load switch ILIM pin,
		 * setting a minimum OCP current of 3186 mA.
		 * Putting an internal pull-up on USB_Cx_5V_EN, effectively put
		 * a 33k resistor on ILIM, setting a minimum OCP current of
		 * 1505 mA.
		 */
		int flags = (vbus_rp[port] == TYPEC_RP_1A5 && vbus_en[port]) ?
				    (GPIO_INPUT | GPIO_PULL_UP) :
				    (GPIO_OUTPUT | GPIO_PULL_UP);
		gpio_set_level(gpio_5v_en, vbus_en[port]);
		gpio_set_flags(gpio_5v_en, flags);
	}
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	vbus_rp[port] = rp;

	/* change the GPIO driving the load switch if needed */
	board_vbus_update_source_current(port);
}

int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	gpio_set_level(port ? GPIO_USB_C1_CHARGE_L : GPIO_USB_C0_CHARGE_L, 1);

	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);

	/* Provide VBUS */
	vbus_en[port] = 1;
	board_vbus_update_source_current(port);

	if (system_get_board_version() >= 2)
		pd_set_vbus_discharge(port, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = vbus_en[port];

	/* Disable VBUS */
	vbus_en[port] = 0;
	board_vbus_update_source_current(port);

	/* Enable discharge if we were previously sourcing 5V */
	if (system_get_board_version() >= 2 && prev_en)
		pd_set_vbus_discharge(port, 1);

	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_snk_is_vbus_provided(int port)
{
	return !gpio_get_level(port ? GPIO_USB_C1_VBUS_WAKE_L :
				      GPIO_USB_C0_VBUS_WAKE_L);
}

int pd_check_vconn_swap(int port)
{
	/* in G3, do not allow vconn swap since pp5000_A rail is off */
	return gpio_get_level(GPIO_PMIC_SLP_SUS_L);
}

__override void pd_execute_data_swap(int port, enum pd_data_role data_role)
{
	/* Only port 0 supports device mode. */
	if (port != 0)
		return;

	gpio_set_level(GPIO_USB2_OTG_ID, (data_role == PD_ROLE_UFP) ? 1 : 0);
	gpio_set_level(GPIO_USB2_OTG_VBUSSENSE,
		       (data_role == PD_ROLE_UFP) ? 1 : 0);
}
