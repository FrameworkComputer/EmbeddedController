/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Geralt baseboard-specific USB-C configuration */

#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "driver/tcpm/it83xx_pd.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"
#include "zephyr_adc.h"

#include <zephyr/drivers/gpio.h>

#include <ap_power/ap_power.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t
		cc_parameter[CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT] = {
			{
				.rising_time =
					IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
				.falling_time =
					IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
			},
			{
				.rising_time =
					IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
				.falling_time =
					IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
			},
		};

	return &cc_parameter[port];
}
#endif

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

int board_set_active_charge_port(int port)
{
	int i;
	int is_valid_port = (port >= 0 && port < board_get_usb_pd_port_count());
	/* adjust the actual port count when not the type-c db connected. */

	if (!is_valid_port && port != CHARGE_PORT_NONE) {
		return EC_ERROR_INVAL;
	}

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0)) {
				CPRINTS("Disabling C%d as sink failed.", i);
			}
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			continue;
		}

		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTS("C%d: sink path disable failed.", i);
		}
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0) {
		return ADC_VBUS_C0;
	}
	if (port == 1) {
		return ADC_VBUS_C1;
	}
	CPRINTS("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */

/* USB-A */
void xhci_interrupt(enum gpio_signal signal)
{
	const int xhci_stat = gpio_get_level(signal);

#ifdef USB_PORT_ENABLE_COUNT
	enum usb_charge_mode mode =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ap_xhci_init_done)) ?
			USB_CHARGE_MODE_ENABLED :
			USB_CHARGE_MODE_DISABLED;

	for (int i = 0; i < USB_PORT_ENABLE_COUNT; i++) {
		usb_charge_set_mode(i, mode, USB_ALLOW_SUSPEND_CHARGE);
	}
#endif

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
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ap_xhci_init_done))) {
		return PD_DRP_TOGGLE_ON;
	} else {
		return PD_DRP_FORCE_SINK;
	}
}
