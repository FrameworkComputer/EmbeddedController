/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-specific USB-C configuration */

#include "adc.h"
#include "baseboard_common.h"
#include "bc12/pi3usb9201_public.h"
#include "button.h"
#include "charger.h"
#include "charge_state_v2.h"
#include "charger/isl923x_public.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
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
#include "tcpm/it8xxx2_pd_public.h"
#include "uart.h"
#include "usbc_ppc.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_mux/ps8743_public.h"
#include "usb_mux/it5205_public.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include "variant_db_detection.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/* Baseboard */
static void baseboard_init(void)
{
	gpio_enable_interrupt(GPIO_USB_C0_PPC_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_AP_XHCI_INIT_DONE);
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT-1);

static void board_tcpc_init(void)
{
	/* C1: GPIO_USB_C1_PPC_INT_ODL & HDMI: GPIO_PS185_EC_DP_HPD */
	gpio_enable_interrupt(GPIO_X_EC_GPIO2);

	/* If this is not a Type-C subboard, disable the task. */
	if (corsola_get_db_type() != CORSOLA_DB_TYPEC)
		task_disable_task(TASK_ID_PD_C1);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

/* PPC */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
		.frs_en = GPIO_USB_C0_PPC_FRSINFO,
	},
	{
		.i2c_port = I2C_PORT_PPC1,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
		.frs_en = GPIO_USB_C1_FRS_EN,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const struct pi3usb9201_config_t
		pi3usb9201_bc12_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	/* [0]: unused */
	[1] = {
		.i2c_port = I2C_PORT_PPC1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	}
};

struct bc12_config bc12_ports[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{ .drv = NULL },
	{ .drv = &pi3usb9201_drv },
};

void bc12_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12);
}

static void board_sub_bc12_init(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC)
		gpio_enable_interrupt(GPIO_USB_C1_BC12_CHARGER_INT_ODL);
	else
		/* If this is not a Type-C subboard, disable the task. */
		task_disable_task(TASK_ID_USB_CHG_P1);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_sub_bc12_init, HOOK_PRIO_INIT_I2C + 1);

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC)
		return CONFIG_USB_PD_PORT_MAX_COUNT;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
}

/* USB-A */
const int usb_port_enable[] = {
	GPIO_EN_PP5000_USB_A0_VBUS,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

void usb_a0_interrupt(enum gpio_signal signal)
{
	enum usb_charge_mode mode = gpio_get_level(signal) ?
		USB_CHARGE_MODE_ENABLED : USB_CHARGE_MODE_DISABLED;

	for (int i = 0; i < USB_PORT_COUNT; i++)
		usb_charge_set_mode(i, mode, USB_ALLOW_SUSPEND_CHARGE);
}

static int board_ps8743_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int reg = 0;

	rv = ps8743_read(me, PS8743_REG_MODE, &reg);
	if (rv)
		return rv;

	/* Disable FLIP pin, enable I2C control. */
	reg |= PS8743_MODE_FLIP_REG_CONTROL;
	/* Disable CE_USB pin, enable I2C control. */
	reg |= PS8743_MODE_USB_REG_CONTROL;
	/* Disable CE_DP pin, enable I2C control. */
	reg |= PS8743_MODE_DP_REG_CONTROL;

	/*
	 * DP specific config
	 *
	 * Enable/Disable IN_HPD on the DB.
	 */
	gpio_set_level(GPIO_USB_C1_DP_IN_HPD,
		       mux_state & USB_PD_MUX_DP_ENABLED);

	return ps8743_write(me, PS8743_REG_MODE, reg);
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

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
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
		.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
		.driver = &ps8743_usb_mux_driver,
		.next_mux = &usbc1_virtual_mux,
		.board_set = &board_ps8743_mux_set,
	},
};

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO: check correct operation for Corsola */
}

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

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

/**
 * Handle PS185 HPD changing state.
 */
int debounced_hpd;

static void ps185_hdmi_hpd_deferred(void)
{
	const int new_hpd = gpio_get_level(GPIO_PS185_EC_DP_HPD);

	/* HPD status not changed, probably a glitch, just return. */
	if (debounced_hpd == new_hpd)
		return;

	debounced_hpd = new_hpd;

	gpio_set_level(GPIO_EC_AP_DP_HPD_ODL, !debounced_hpd);
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
		syv682x_interrupt(1);
	else if (sub == CORSOLA_DB_HDMI)
		hdmi_hpd_interrupt(signal);
	else
		CPRINTS("Undetected subboard interrupt.");
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_PPC_BC12_INT_ODL) == 0;
	if (port == 1 && corsola_get_db_type() == CORSOLA_DB_TYPEC)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;

	return 0;
}

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
