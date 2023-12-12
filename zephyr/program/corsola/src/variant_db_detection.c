/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */
#include "baseboard_usbc_config.h"
#include "console.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_mux.h"
#include "variant_db_detection.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#ifdef TEST_BUILD
uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif

static void corsola_db_config(enum corsola_db_type type)
{
	switch (type) {
	case CORSOLA_DB_HDMI:
		/* EC_X_GPIO1 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr),
				      GPIO_OUTPUT_HIGH);
		/* X_EC_GPIO2 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd),
				      GPIO_INPUT);
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));
		/* EC_X_GPIO3 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl),
				      GPIO_OUTPUT_HIGH | GPIO_OPEN_DRAIN);
		return;
	case CORSOLA_DB_TYPEC:
		/* EC_X_GPIO1 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_frs_en),
				      GPIO_OUTPUT_LOW);
		/* X_EC_GPIO2 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_ppc_int_odl),
			GPIO_INPUT | GPIO_PULL_UP);
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));
		/* EC_X_GPIO3 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_dp_in_hpd),
				      GPIO_OUTPUT_LOW);
		return;
	case CORSOLA_DB_NONE:
		/* Set floating pins as input with PU to prevent leakage */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_x_gpio1),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_x_ec_gpio2),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_x_gpio3),
				      GPIO_INPUT | GPIO_PULL_UP);
		return;
	default:
		break;
	}
}

enum corsola_db_type corsola_get_db_type(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(db_config))
	int ret;
	uint32_t val;
#endif
	static enum corsola_db_type db = CORSOLA_DB_UNINIT;

	if (db != CORSOLA_DB_UNINIT) {
		return db;
	}

	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hdmi_prsnt_odl))) {
		db = CORSOLA_DB_HDMI;
	} else {
		db = CORSOLA_DB_TYPEC;
	}

/* Detect for no sub board case by FW_CONFIG */
#if DT_NODE_EXISTS(DT_NODELABEL(db_config))
	ret = cros_cbi_get_fw_config(DB, &val);
	if (ret != 0) {
		CPRINTS("Error retrieving CBI FW_CONFIG field %d", DB);
	} else if (val == DB_NONE) {
		db = CORSOLA_DB_NONE;
	} else if (val == DB_USBA_HDMI) {
		db = CORSOLA_DB_HDMI;
	}
#endif

	corsola_db_config(db);

	switch (db) {
	case CORSOLA_DB_NONE:
		CPRINTS("Detect %s DB", "NONE");
		break;
	case CORSOLA_DB_TYPEC:
		CPRINTS("Detect %s DB", "TYPEC");
		break;
	case CORSOLA_DB_HDMI:
		CPRINTS("Detect %s DB", "HDMI");
		break;
	default:
		CPRINTS("DB UNINIT");
		break;
	}

	return db;
}

static void corsola_db_init(void)
{
	corsola_get_db_type();
}
DECLARE_HOOK(HOOK_INIT, corsola_db_init, HOOK_PRIO_PRE_I2C);

/**
 * Handle PS185 HPD changing state.
 */
void ps185_hdmi_hpd_mux_set(void)
{
	const int hpd =
		gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd));

	if (!corsola_is_dp_muxable(USBC_PORT_C1)) {
		return;
	}

	if (hpd && !(usb_mux_get(USBC_PORT_C1) & USB_PD_MUX_DP_ENABLED)) {
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
		usb_mux_set(USBC_PORT_C1, USB_PD_MUX_DP_ENABLED,
			    USB_SWITCH_DISCONNECT,
			    0 /* polarity, don't care */);
		CPRINTS("HDMI plug");
	}
}

static void ps185_hdmi_hpd_deferred(void)
{
	const int hpd =
		gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd));

	if (!hpd && (usb_mux_get(USBC_PORT_C1) & USB_PD_MUX_DP_ENABLED)) {
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
		CPRINTS("HDMI unplug");

		return;
	}

	ps185_hdmi_hpd_mux_set();
}
DECLARE_DEFERRED(ps185_hdmi_hpd_deferred);

#define HPD_SINK_ABSENCE_DEBOUNCE (2 * MSEC)

static void hdmi_hpd_interrupt_deferred(void)
{
	const int hpd =
		gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd));

	/* C0 DP is muxed, we should not send HPD to the AP */
	if (!corsola_is_dp_muxable(USBC_PORT_C1)) {
		if (hpd) {
			CPRINTS("C0 port is already muxed.");
		}
		return;
	}

	if (hpd && !(usb_mux_get(USBC_PORT_C1) & USB_PD_MUX_DP_ENABLED)) {
		/* set dp_aux_path_sel first, and configure the usb_mux in the
		 * deferred hook to prevent from dead locking.
		 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel), hpd);
		hook_call_deferred(&ps185_hdmi_hpd_deferred_data, 0);
	}

	svdm_set_hpd_gpio(USBC_PORT_C1, hpd);
}
DECLARE_DEFERRED(hdmi_hpd_interrupt_deferred);

void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	const int hpd =
		gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd));

	if (!hpd) {
		hook_call_deferred(&ps185_hdmi_hpd_deferred_data,
				   HPD_SINK_ABSENCE_DEBOUNCE);
	} else {
		hook_call_deferred(&ps185_hdmi_hpd_deferred_data, -1);
	}

	hook_call_deferred(&hdmi_hpd_interrupt_deferred_data, 0);
}
