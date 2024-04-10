/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kingoftown board-specific USB-C configuration */

#include "bc12/pi3usb9201_public.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "common.h"
#include "config.h"
#include "gpio.h"
#include "hooks.h"
#include "ppc/sn5s330_public.h"
#include "system.h"
#include "tcpm/ps8xxx_public.h"
#include "tcpm/tcpci.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_config.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

int charger_profile_override(struct charge_state_data *curr)
{
	int usb_mv;
	int port;

	if (curr->state != ST_CHARGE)
		return 0;

	/* Lower the max requested voltage to 5V when battery is full. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
	    !(curr->batt.flags & BATT_FLAG_BAD_STATUS) &&
	    !(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    (curr->batt.status & STATUS_FULLY_CHARGED))
		usb_mv = 5000;
	else
		usb_mv = PD_MAX_VOLTAGE_MV;

	if (pd_get_max_voltage() != usb_mv) {
		CPRINTS("VBUS limited to %dmV", usb_mv);
		for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++)
			pd_set_external_voltage_limit(port, usb_mv);
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}

/* GPIO Interrupt Handlers */
void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void usb0_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

void usb1_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

static void usba_oc_deferred(void)
{
	/* Use next number after all USB-C ports to indicate the USB-A port */
	board_overcurrent_event(CONFIG_USB_PD_PORT_MAX_COUNT,
				!gpio_get_level(GPIO_USB_A0_OC_ODL));
}
DECLARE_DEFERRED(usba_oc_deferred);

void usba_oc_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&usba_oc_deferred_data, 0);
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_SWCTL_INT_ODL:
		sn5s330_interrupt(0);
		break;
	case GPIO_USB_C1_SWCTL_INT_ODL:
		sn5s330_interrupt(1);
		break;
	default:
		break;
	}
}

/* Power Path Controller */
struct ppc_config_t ppc_chips[] = {
	{ .i2c_port = I2C_PORT_TCPC0,
	  .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	  .drv = &sn5s330_drv },
	{ .i2c_port = I2C_PORT_TCPC1,
	  .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	  .drv = &sn5s330_drv },
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

/*
 * Port-0/1 USB mux driver.
 *
 * The USB mux is handled by TCPC chip and the HPD update is through a GPIO
 * to AP. But the TCPC chip is also needed to know the HPD status; otherwise,
 * the mux misbehaves.
 */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.driver = &tcpci_tcpm_usb_mux_driver,
				.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.driver = &tcpci_tcpm_usb_mux_driver,
				.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			},
	}
};

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

/* BC1.2 */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_POWER,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	{
		.i2c_port = I2C_PORT_EEPROM,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/* Initialize board USC-C things */
static void board_init_usbc(void)
{
	/* Enable BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable USB-A overcurrent interrupt */
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init_usbc, HOOK_PRIO_DEFAULT);

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		board_reset_pd_mcu();
	}

	/* Enable PPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_SWCTL_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_SWCTL_INT_ODL);

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

void board_reset_pd_mcu(void)
{
	cprints(CC_USB, "Resetting TCPCs...");
	cflush();

	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	crec_msleep(PS8XXX_RESET_DELAY_MS);
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
	crec_msleep(PS8805_FW_INIT_DELAY_MS);
}

void board_set_tcpc_power_mode(int port, int mode)
{
	/* Ignore the "mode" to turn the chip on.  We can only do a reset. */
	if (mode)
		return;

	board_reset_pd_mcu();
}

int board_vbus_sink_enable(int port, int enable)
{
	/* Both ports are controlled by PPC SN5S330 */
	return ppc_vbus_sink_enable(port, enable);
}

int board_is_sourcing_vbus(int port)
{
	/* Both ports are controlled by PPC SN5S330 */
	return ppc_is_sourcing_vbus(port);
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO(b/120231371): Notify AP */
	CPRINTS("p%d: overcurrent!", port);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charging port");

		/* Disable all ports. */
		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (board_vbus_sink_enable(i, 0))
				CPRINTS("Disabling p%d sink path failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: p%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (i == port)
			continue;

		if (board_vbus_sink_enable(i, 0))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (board_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/*
	 * Ignore lower charge ceiling on PD transition if our battery is
	 * critical, as we may brownout.
	 */
	if (supplier == CHARGE_SUPPLIER_PD && charge_ma < 1500 &&
	    charge_get_percent() < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		CPRINTS("Using max ilim %d", max_ma);
		charge_ma = max_ma;
	}

	charge_set_input_current_limit(charge_ma, charge_mv);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}
