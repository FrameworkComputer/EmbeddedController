/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Krabby board-specific USB-C configuration */

#include "adc.h"
#include "baseboard_usbc_config.h"
#include "bc12/pi3usb9201_public.h"
#include "charge_manager.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/rt9490.h"
#include "driver/ppc/rt1739.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/usb_mux/tusb1064.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ppc/syv682x_public.h"
#include "usb_mux/it5205_public.h"
#include "usbc_ppc.h"

#include "variant_db_detection.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* charger */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = RT9490_ADDR_FLAGS,
		.drv = &rt9490_drv,
	},
};

void c0_bc12_interrupt(enum gpio_signal signal)
{
	rt1739_interrupt(0);
}

void c1_bc12_interrupt(enum gpio_signal signal)
{
	rt9490_interrupt(1);
}


static void board_sub_bc12_init(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC)
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12_charger));
	else
		/* If this is not a Type-C subboard, disable the task. */
		task_disable_task(TASK_ID_USB_CHG_P1);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_sub_bc12_init, HOOK_PRIO_POST_I2C);

static void board_usbc_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc_bc12));
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_DEFAULT + 1);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl)))
		syv682x_interrupt(1);
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(usb_c0_ppc_bc12_int_odl)) == 0;
	if (port == 1 && corsola_get_db_type() == CORSOLA_DB_TYPEC)
		return gpio_pin_get_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_ppc_int_odl)) == 0;

	return 0;
}

const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t
		cc_parameter[CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT] = {
		{
			.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
			.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
		},
		{
			.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
			.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
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

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTS("Disabling C%d as sink failed.", i);
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
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTS("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

const struct usb_mux usbc0_virtual_mux = {
	.usb_port = 0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};
const struct usb_mux usbc1_virtual_mux = {
	.usb_port = 1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_MUX0,
		.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
		.driver = &it5205_usb_mux_driver,
		.next_mux = &usbc0_virtual_mux,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_MUX1,
		.i2c_addr_flags = TUSB1064_I2C_ADDR0_FLAGS,
		.driver = &tusb1064_usb_mux_driver,
		.hpd_update = &tusb1044_hpd_update,
		.next_mux = &usbc1_virtual_mux,
	},
};

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0)
		return  ADC_VBUS_C0;
	if (port == 1)
		return  ADC_VBUS_C1;
	CPRINTSUSB("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */
