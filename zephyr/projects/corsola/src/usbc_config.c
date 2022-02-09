/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-specific USB-C configuration */

#include "adc.h"
#include "baseboard_usbc_config.h"
#include "button.h"
#include "charger.h"
#include "charge_state_v2.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "task.h"
#include "ppc/syv682x_public.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "tablet_mode.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include "variant_db_detection.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Baseboard */
static void baseboard_init(void)
{
#ifdef CONFIG_VARIANT_CORSOLA_USBA
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usba));
#endif
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT-1);

static void baseboard_tcpc_init(void)
{
	/* If this is not a Type-C subboard, disable the task. */
	if (corsola_get_db_type() != CORSOLA_DB_TYPEC)
		task_disable_task(TASK_ID_PD_C1);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

static void baseboard_x_ec_gpio2_init(void)
{
	/* type-c: USB_C1_PPC_INT_ODL / hdmi: PS185_EC_DP_HPD */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));
}
DECLARE_HOOK(HOOK_INIT, baseboard_x_ec_gpio2_init, HOOK_PRIO_DEFAULT);

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC)
		return CONFIG_USB_PD_PORT_MAX_COUNT;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
}

/* USB-A */
const int usb_port_enable[] = {
	GPIO_SIGNAL(DT_NODELABEL(en_pp5000_usb_a0_vbus))
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

void usb_a0_interrupt(enum gpio_signal signal)
{
	enum usb_charge_mode mode = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done)) ?
		USB_CHARGE_MODE_ENABLED : USB_CHARGE_MODE_DISABLED;

	for (int i = 0; i < USB_PORT_COUNT; i++)
		usb_charge_set_mode(i, mode, USB_ALLOW_SUSPEND_CHARGE);
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

/**
 * Handle PS185 HPD changing state.
 */
int debounced_hpd;

static void ps185_hdmi_hpd_deferred(void)
{
	const int new_hpd = gpio_pin_get_dt(
				GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd));

	/* HPD status not changed, probably a glitch, just return. */
	if (debounced_hpd == new_hpd)
		return;

	debounced_hpd = new_hpd;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_dp_hpd_odl),
			!debounced_hpd);
	CPRINTS(debounced_hpd ? "HDMI plug" : "HDMI unplug");
}
DECLARE_DEFERRED(ps185_hdmi_hpd_deferred);

#define PS185_HPD_DEBOUCE 250

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ps185_hdmi_hpd_deferred_data, PS185_HPD_DEBOUCE);
}

/* HDMI/TYPE-C function shared subboard interrupt */
void x_ec_interrupt(enum gpio_signal signal)
{
	int sub = corsola_get_db_type();

	if (sub == CORSOLA_DB_TYPEC)
		/* C1: PPC interrupt */
		ppc_interrupt(signal);
	else if (sub == CORSOLA_DB_HDMI)
		hdmi_hpd_interrupt(signal);
	else
		CPRINTS("Undetected subboard interrupt.");
}
