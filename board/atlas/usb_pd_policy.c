/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "extpower.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/ps8xxx.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpci.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint8_t vbus_en[CONFIG_USB_PD_PORT_MAX_COUNT];
static uint8_t vbus_rp[CONFIG_USB_PD_PORT_MAX_COUNT] = {TYPEC_RP_1A5,
							TYPEC_RP_1A5};

int board_vbus_source_enabled(int port)
{
	return vbus_en[port];
}

static void board_vbus_update_source_current(int port)
{
	enum gpio_signal gpio_5v_en = port ? GPIO_USB_C1_5V_EN :
		GPIO_USB_C0_5V_EN;
	enum gpio_signal gpio_3a_en = port ? GPIO_EN_USB_C1_3A :
		GPIO_EN_USB_C0_3A;

	/*
	 * 1.5 vs 3.0 A limit is controlled by a dedicated gpio where
	 * high = 3.0A and low = 1.5A. VBUS on/off is controlled by
	 * GPIO_USB_C0/1_5V_EN.
	 */
	gpio_set_level(gpio_3a_en, vbus_rp[port] == TYPEC_RP_3A0 ? 1 : 0);
	gpio_set_level(gpio_5v_en, vbus_en[port]);
}

void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	vbus_rp[port] = rp;

	/* change the GPIO driving the load switch if needed */
	board_vbus_update_source_current(port);
}

int pd_snk_is_vbus_provided(int port)
{
	return tcpci_tcpm_check_vbus_level(port, VBUS_PRESENT);
}

int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	gpio_set_level(port ? GPIO_EN_USB_C1_CHARGE_L :
			      GPIO_EN_USB_C0_CHARGE_L, 1);

	/* Ensure we advertise the proper available current quota */
	charge_manager_source_port(port, 1);

	pd_set_vbus_discharge(port, 0);
	/* Provide VBUS */
	vbus_en[port] = 1;
	board_vbus_update_source_current(port);

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
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	/* Give back the current quota we are no longer using */
	charge_manager_source_port(port, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* in G3, do not allow vconn swap since pp5000_A rail is off */
	return gpio_get_level(GPIO_PMIC_SLP_SUS_L);
}

__override void pd_execute_data_swap(int port,
				     enum pd_data_role data_role)
{
	/* Only port 0 supports device mode. */
	if (port != 0)
		return;

	gpio_set_level(GPIO_USB2_ID,
		      (data_role == PD_ROLE_UFP) ? 1 : 0);
	gpio_set_level(GPIO_USB2_VBUSSENSE,
		      (data_role == PD_ROLE_UFP) ? 1 : 0);
}
