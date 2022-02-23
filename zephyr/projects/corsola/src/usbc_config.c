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

/* a flag for indicating the tasks are inited. */
static bool tasks_inited;

/* Baseboard */
static void baseboard_init(void)
{
#ifdef CONFIG_VARIANT_CORSOLA_USBA
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usba));
#endif
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT-1);

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_HDMI) {
		if (tasks_inited)
			return CONFIG_USB_PD_PORT_MAX_COUNT;
		else
			return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	}

	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

/* USB-A */
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

	if (!corsola_is_dp_muxable(USBC_PORT_C1)) {
		if (debounced_hpd)
			CPRINTS("C0 port is already muxed.");
		return;
	}

	if (debounced_hpd) {
		dp_status[USBC_PORT_C1] =
			VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				      0, /* HPD level ... not applicable */
				      0, /* exit DP? ... no */
				      0, /* usb mode? ... no */
				      0, /* multi-function ... no */
				      1, /* DP enabled ... yes */
				      0, /* power low?  ... no */
				      (!!DP_FLAGS_DP_ON));
		/* update C1 virtual mux */
		usb_mux_set(USBC_PORT_C1,
			    USB_PD_MUX_DP_ENABLED,
			    USB_SWITCH_DISCONNECT,
			    0 /* polarity, don't care */);

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel),
				debounced_hpd);
		CPRINTS("Set DP_AUX_PATH_SEL: %d", 1);
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_dp_hpd_odl),
			!debounced_hpd);
	CPRINTS(debounced_hpd ? "HDMI plug" : "HDMI unplug");
}
DECLARE_DEFERRED(ps185_hdmi_hpd_deferred);

static void ps185_hdmi_hpd_disconnect_deferred(void)
{
	const int new_hpd = gpio_pin_get_dt(
				GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd));

	if (debounced_hpd == new_hpd && !new_hpd) {
		dp_status[USBC_PORT_C1] =
			VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				      0, /* HPD level ... not applicable */
				      0, /* exit DP? ... no */
				      0, /* usb mode? ... no */
				      0, /* multi-function ... no */
				      0, /* DP enabled ... no */
				      0, /* power low?  ... no */
				      (!DP_FLAGS_DP_ON));
		usb_mux_set(USBC_PORT_C1, USB_PD_MUX_NONE,
			    USB_SWITCH_DISCONNECT,
			    0 /* polarity, don't care */);
	}

}
DECLARE_DEFERRED(ps185_hdmi_hpd_disconnect_deferred);

#define PS185_HPD_DEBOUCE 250
#define HPD_SINK_ABSENCE_DEBOUNCE (2 * MSEC)

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ps185_hdmi_hpd_deferred_data, PS185_HPD_DEBOUCE);

	if (!gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd)))
		hook_call_deferred(&ps185_hdmi_hpd_disconnect_deferred_data,
				   HPD_SINK_ABSENCE_DEBOUNCE);
	else
		hook_call_deferred(&ps185_hdmi_hpd_disconnect_deferred_data,
				   -1);
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

void board_hdmi_suspend(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_HDMI)
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_hdmi_suspend, HOOK_PRIO_DEFAULT);

void board_hdmi_resume(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_HDMI)
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_hdmi_resume, HOOK_PRIO_DEFAULT);

static void tasks_init_deferred(void)
{
	tasks_inited = true;
}
DECLARE_DEFERRED(tasks_init_deferred);

static void baseboard_x_ec_gpio2_init(void)
{
	static struct ppc_drv virtual_ppc_drv = { 0 };
	static struct tcpm_drv virtual_tcpc_drv = { 0 };
	static struct bc12_drv virtual_bc12_drv = { 0 };

	/* type-c: USB_C1_PPC_INT_ODL / hdmi: PS185_EC_DP_HPD */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));

	if (corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		gpio_pin_interrupt_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_ppc_int_odl),
			GPIO_INT_EDGE_FALLING);
		return;
	}

	/* drop related C1 port drivers when it's a HDMI DB. */
	ppc_chips[USBC_PORT_C1] =
		(const struct ppc_config_t){ .drv = &virtual_ppc_drv };
	tcpc_config[USBC_PORT_C1] =
		(const struct tcpc_config_t){ .drv = &virtual_tcpc_drv };
	bc12_ports[USBC_PORT_C1] =
		(const struct bc12_config){ .drv = &virtual_bc12_drv };
	/* Use virtual mux to notify AP the mainlink direction. */
	usb_muxes[USBC_PORT_C1] = (struct usb_mux){
		.usb_port = USBC_PORT_C1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	};

	/*
	 * If a HDMI DB is attached, C1 port tasks will be exiting in that
	 * the port number is larger than board_get_usb_pd_port_count().
	 * After C1 port tasks finished, we intentionally increase the port
	 * count by 1 for usb_mux to access the C1 virtual mux for notifying
	 * mainlink direction.
	 */
	hook_call_deferred(&tasks_init_deferred_data, 2 * SECOND);
}
DECLARE_HOOK(HOOK_INIT, baseboard_x_ec_gpio2_init, HOOK_PRIO_DEFAULT);

__override uint8_t get_dp_pin_mode(int port)
{
	if (corsola_get_db_type() == CORSOLA_DB_HDMI && port == USBC_PORT_C1)
		return MODE_DP_PIN_E;

	return pd_dfp_dp_get_pin_mode(port, dp_status[port]);
}
