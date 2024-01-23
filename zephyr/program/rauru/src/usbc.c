/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "console.h"
#include "usb_pd.h"
#include "zephyr_adc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)

int board_set_active_charge_port(int port)
{
	/* TODO(b:308941437): implement me */
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	/* TODO(b:308941437): implement me */
	return 0;
}

void pd_power_supply_reset(int port)
{
	/* TODO(b:308941437): implement me */
}

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

int pd_check_vconn_swap(int port)
{
	/* TODO(b:308941437): implement me */
	return 0;
}

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0) {
		return ADC_VBUS_C0;
	} else if (port == 1) {
		return ADC_VBUS_C1;
	}
	CPRINTSUSB("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */
