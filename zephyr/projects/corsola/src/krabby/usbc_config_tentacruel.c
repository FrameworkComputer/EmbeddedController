/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tentacruel board-specific USB-C configuration */

#include "adc.h"
#include "baseboard_usbc_config.h"
#include "bc12/pi3usb9201_public.h"
#include "charge_manager.h"
#include "charger.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/charger/rt9490.h"
#include "driver/ppc/rt1739.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/usb_mux/ps8743.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ppc/syv682x_public.h"
#include "usb_mux/it5205_public.h"
#include "usbc_ppc.h"
#include "usbc/ppc.h"

#include "variant_db_detection.h"
#include <zephyr/logging/log.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

LOG_MODULE_REGISTER(alt_dev_replacement);

#define BOARD_VERSION_UNKNOWN 0xffffffff

/* Check board version to decide which ppc/bc12 is used. */
static bool board_has_syv_ppc(void)
{
	static uint32_t board_version = BOARD_VERSION_UNKNOWN;

	if (board_version == BOARD_VERSION_UNKNOWN) {
		if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
			LOG_ERR("Failed to get board version.");
			board_version = 0;
		}
	}

	return (board_version >= 3);
}

static void check_alternate_devices(void)
{
	/* Configure the PPC driver */
	if (board_has_syv_ppc())
		/* Arg is the USB port number */
		PPC_ENABLE_ALTERNATE(0);
}
DECLARE_HOOK(HOOK_INIT, check_alternate_devices, HOOK_PRIO_DEFAULT);

void bc12_interrupt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void board_usbc_init(void)
{
	if (board_has_syv_ppc()) {
		/* Enable PPC interrupts. */
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));

		/* Enable BC1.2 interrupts. */
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	} else {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	}
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (board_has_syv_ppc()) {
		if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_int_odl))) {
			syv682x_interrupt(0);
		}
	} else {
		rt1739_interrupt(0);
	}

	if (signal == GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl))) {
		syv682x_interrupt(1);
	}
}

int ppc_get_alert_status(int port)
{
	if (port == 0) {
		return gpio_pin_get_dt(
			       GPIO_DT_FROM_NODELABEL(usb_c0_ppc_int_odl)) == 0;
	}
	if (port == 1 && corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		return gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(
			       gpio_usb_c1_ppc_int_odl)) == 0;
	}

	return 0;
}

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

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO: check correct operation for Corsola */
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
	return 0;
}

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

	if (!is_valid_port && port != CHARGE_PORT_NONE) {
		return EC_ERROR_INVAL;
	}

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
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
	for (i = 0; i < ppc_cnt; i++) {
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
	CPRINTSUSB("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */
