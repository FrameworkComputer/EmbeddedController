/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "extpower.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "driver/charger/bd9995x.h"
#include "driver/tcpm/anx74xx.h"
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
	enum gpio_signal gpio = port ? GPIO_USB_C1_5V_EN : GPIO_USB_C0_5V_EN;
	enum gpio_signal gpio_3a_en = port ? GPIO_EN_USB_C1_3A :
		GPIO_EN_USB_C0_3A;

	gpio_set_level(gpio_3a_en, vbus_rp[port] == TYPEC_RP_3A0 ? 1 : 0);
	gpio_set_level(gpio, vbus_en[port]);
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	vbus_rp[port] = rp;

	/* change the GPIO driving the load switch if needed */
	board_vbus_update_source_current(port);
}

int pd_set_power_supply_ready(int port)
{
	/* Ensure we're not charging from this port */
	bd9995x_select_input_port(port, 0);

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
	return gpio_get_level(GPIO_EN_PP5000);
}
