/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-specific USB-C configuration */

#include "baseboard_usbc_config.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usb_tc_sm.h"
#include "usbc/usb_muxes.h"
#include "usbc_ppc.h"
#include "variant_db_detection.h"

#include <zephyr/drivers/gpio.h>

#include <ap_power/ap_power.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/* a flag for indicating the tasks are inited. */
test_export_static bool tasks_inited;

/* Baseboard */
test_export_static void baseboard_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_xhci));

	/* If CCD mode has enabled before init, force the ccd_interrupt. */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ccd_mode_odl))) {
		ccd_interrupt(GPIO_CCD_MODE_ODL);
	}
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ccd_mode_odl));
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_PRE_DEFAULT);

__override uint8_t board_get_usb_pd_port_count(void)
{
	/* This function returns the PORT_COUNT when HDMI db is connected.
	 * This is a trick to ensure the usb_mux_set being set properley.
	 * HDMI display functions using the USB virtual mux to * communicate
	 * with the DP bridge.
	 */
	if (corsola_get_db_type() == CORSOLA_DB_HDMI) {
		if (tasks_inited) {
			return CONFIG_USB_PD_PORT_MAX_COUNT;
		} else {
			return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
		}
	} else if (corsola_get_db_type() == CORSOLA_DB_NONE) {
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	}

	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

uint8_t board_get_adjusted_usb_pd_port_count(void)
{
	const enum corsola_db_type db = corsola_get_db_type();

	if (db == CORSOLA_DB_TYPEC || db == CORSOLA_DB_NO_DETECTION) {
		return CONFIG_USB_PD_PORT_MAX_COUNT;
	} else {
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	}
}

/* USB-A */
void xhci_interrupt(enum gpio_signal signal)
{
	const int xhci_stat = gpio_get_level(signal);

#ifdef USB_PORT_ENABLE_COUNT
	enum usb_charge_mode mode = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
					    gpio_ap_xhci_init_done)) ?
					    USB_CHARGE_MODE_ENABLED :
					    USB_CHARGE_MODE_DISABLED;

	for (int i = 0; i < USB_PORT_COUNT; i++) {
		usb_charge_set_mode(i, mode, USB_ALLOW_SUSPEND_CHARGE);
	}
#endif /* USB_PORT_ENABLE_COUNT */

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/*
		 * Enable DRP toggle after XHCI inited. This is used to follow
		 * USB 3.2 spec 10.3.1.1.
		 */
		if (xhci_stat) {
			pd_set_dual_role(i, PD_DRP_TOGGLE_ON);
		} else if (tc_is_attached_src(i)) {
			/*
			 * This is a AP reset S0->S0 transition.
			 * We should set the role back to sink.
			 */
			pd_set_dual_role(i, PD_DRP_FORCE_SINK);
		}
	}
}

__override enum pd_dual_role_states pd_get_drp_state_in_s0(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done))) {
		return PD_DRP_TOGGLE_ON;
	} else {
		return PD_DRP_FORCE_SINK;
	}
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

/* HDMI/TYPE-C function shared subboard interrupt */
void x_ec_interrupt(enum gpio_signal signal)
{
	int sub = corsola_get_db_type();

	if (sub == CORSOLA_DB_TYPEC) {
		/* C1: PPC interrupt */
		ppc_interrupt(signal);
	} else if (sub == CORSOLA_DB_HDMI) {
		hdmi_hpd_interrupt(signal);
	} else {
		CPRINTS("Undetected subboard interrupt.");
	}
}

#ifdef CONFIG_VARIANT_CORSOLA_DB_DETECTION
static void board_hdmi_handler(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		value = 1;
		break;

	case AP_POWER_SUSPEND:
		value = 0;
		break;
	}
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr), value);
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl), value);
}
#endif /* CONFIG_VARIANT_CORSOLA_DB_DETECTION */

static void tasks_init_deferred(void)
{
	tasks_inited = true;
	if (corsola_get_db_type() == CORSOLA_DB_HDMI) {
		/* If the HDMI port is plugged on-boot, and the usb_mux won't
		 * be configured before the task inited.  Re-invoke the
		 * HPD configuration after task inited.
		 */
		ps185_hdmi_hpd_mux_set();
	}
}
DECLARE_DEFERRED(tasks_init_deferred);

#ifdef CONFIG_VARIANT_CORSOLA_DB_DETECTION
test_export_static void baseboard_x_ec_gpio2_init(void)
{
	static struct ppc_drv virtual_ppc_drv = { 0 };
	static struct tcpm_drv virtual_tcpc_drv = { 0 };
#ifdef CONFIG_PLATFORM_EC_USB_CHARGER
	static struct bc12_drv virtual_bc12_drv = { 0 };
#endif

	/* no sub board */
	if (corsola_get_db_type() == CORSOLA_DB_NONE) {
		return;
	}

	/* type-c: USB_C1_PPC_INT_ODL / hdmi: PS185_EC_DP_HPD */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));

	if (corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		gpio_pin_interrupt_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_ppc_int_odl),
			GPIO_INT_EDGE_FALLING);
		return;
	}
	if (corsola_get_db_type() == CORSOLA_DB_HDMI) {
		static struct ap_power_ev_callback cb;

		ap_power_ev_init_callback(&cb, board_hdmi_handler,
					  AP_POWER_RESUME | AP_POWER_SUSPEND);
		ap_power_ev_add_callback(&cb);
	}

	/* drop related C1 port drivers when it's a HDMI DB. */
	ppc_chips[USBC_PORT_C1] =
		(const struct ppc_config_t){ .drv = &virtual_ppc_drv };
	tcpc_config[USBC_PORT_C1] =
		(const struct tcpc_config_t){ .drv = &virtual_tcpc_drv };
#ifdef CONFIG_PLATFORM_EC_USB_CHARGER
	bc12_ports[USBC_PORT_C1] =
		(const struct bc12_config){ .drv = &virtual_bc12_drv };
#endif
	/* Use virtual mux to notify AP the mainlink direction. */
	USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_hdmi_db);

	/*
	 * If a HDMI DB is attached, C1 port tasks will be exiting in that
	 * the port number is larger than board_get_usb_pd_port_count().
	 * After C1 port tasks finished, we intentionally increase the port
	 * count by 1 for usb_mux to access the C1 virtual mux for notifying
	 * mainlink direction.
	 * The current delay is set to 500 ms. cros-ec-typec module will
	 * not be getting the fake type-c port count if the deferred call is
	 * later than the kernel upstarts.
	 */
	hook_call_deferred(&tasks_init_deferred_data, 500 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, baseboard_x_ec_gpio2_init, HOOK_PRIO_DEFAULT);

__override uint8_t get_dp_pin_mode(int port)
{
	if (corsola_get_db_type() == CORSOLA_DB_HDMI && port == USBC_PORT_C1) {
		if (usb_mux_get(USBC_PORT_C1) & USB_PD_MUX_DP_ENABLED) {
			return MODE_DP_PIN_E;
		} else {
			return 0;
		}
	}

	return pd_dfp_dp_get_pin_mode(port, dp_status[port]);
}
#endif /* CONFIG_VARIANT_CORSOLA_DB_DETECTION */
