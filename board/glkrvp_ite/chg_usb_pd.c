/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state_v2.h"
#include "console.h"
#include "hooks.h"
#include "task.h"
#include "tcpci.h"
#include "system.h"
#include "usb_mux.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PTN5110_EXT_GPIO_CONFIG		0x92
#define PTN5110_EXT_GPIO_CONTROL	0x93

#define PTN5110_EXT_GPIO_FRS_EN			BIT(6)
#define PTN5110_EXT_GPIO_EN_SRC			BIT(5)
#define PTN5110_EXT_GPIO_EN_SNK1		BIT(4)
#define PTN5110_EXT_GPIO_IILIM_5V_VBUS_L	BIT(3)

enum glkrvp_charge_ports {
	TYPE_C_PORT_0,
	TYPE_C_PORT_1,
	DC_JACK_PORT_0 = DEDICATED_CHARGE_PORT,
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = IT83XX_I2C_CH_B,
			.addr_flags = 0x50,
		},
		.drv = &tcpci_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = IT83XX_I2C_CH_B,
			.addr_flags = 0x52,
		},
		.drv = &tcpci_tcpm_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.port_addr = 0x10,
		.driver = &ps874x_usb_mux_driver,
	},
	{
		.port_addr = 0x11,
		.driver = &ps874x_usb_mux_driver,
	},
};

/* TODO: Implement this function and move to appropriate file */
void usb_charger_set_switches(int port, enum usb_switch setting)
{
}

static int board_charger_port_is_sourcing_vbus(int port)
{
	int reg;

	/* DC Jack can't source VBUS */
	if (port == DC_JACK_PORT_0)
		return 0;

	if (tcpc_read(port, PTN5110_EXT_GPIO_CONTROL, &reg))
		return 0;

	return !!(reg & PTN5110_EXT_GPIO_EN_SRC);
}

static int ptn5110_ext_gpio_enable(int port, int enable, int gpio)
{
	int reg;
	int rv;

	rv = tcpc_read(port, PTN5110_EXT_GPIO_CONTROL, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= gpio;
	else
		reg &= ~gpio;

	return tcpc_write(port, PTN5110_EXT_GPIO_CONTROL, reg);
}

void board_charging_enable(int port, int enable)
{
	ptn5110_ext_gpio_enable(port, enable, PTN5110_EXT_GPIO_EN_SNK1);
}

void board_vbus_enable(int port, int enable)
{
	ptn5110_ext_gpio_enable(port, enable, PTN5110_EXT_GPIO_EN_SRC);
}

void tcpc_alert_event(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/* Enable TCPC0/1 interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

int board_tcpc_post_init(int port)
{
	int reg;
	int rv;

	rv = tcpc_read(port, PTN5110_EXT_GPIO_CONFIG, &reg);
	if (rv)
		return rv;

	/* Configure PTN5110 External GPIOs as output */
	reg |=  PTN5110_EXT_GPIO_EN_SRC | PTN5110_EXT_GPIO_EN_SNK1 |
		PTN5110_EXT_GPIO_IILIM_5V_VBUS_L;
	rv = tcpc_write(port, PTN5110_EXT_GPIO_CONFIG, reg);
	if (rv)
		return rv;

	return ptn5110_ext_gpio_enable(port, 1,
					PTN5110_EXT_GPIO_IILIM_5V_VBUS_L);
}

/* Reset PD MCU */
void board_reset_pd_mcu(void)
{
	/* TODO: Add reset logic */
}

static inline int board_dc_jack_present(void)
{
	return !gpio_get_level(GPIO_DC_JACK_PRESENT_L);
}

static void board_dc_jack_handle(void)
{
	struct charge_port_info charge_dc_jack;

	/* System is booted from DC Jack */
	if (board_dc_jack_present()) {
		charge_dc_jack.current = (PD_MAX_POWER_MW * 1000) /
					DC_JACK_MAX_VOLTAGE_MV;
		charge_dc_jack.voltage = DC_JACK_MAX_VOLTAGE_MV;
	} else {
		charge_dc_jack.current = 0;
		charge_dc_jack.voltage = USB_CHARGER_VOLTAGE_MV;
	}

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				DC_JACK_PORT_0, &charge_dc_jack);
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_dc_jack_handle, HOOK_PRIO_FIRST);

static void board_charge_init(void)
{
	int port, supplier;

	/* Initialize all charge suppliers to seed the charge manager */
	for (port = 0; port < CHARGE_PORT_COUNT; port++) {
		for (supplier = 0; supplier < CHARGE_SUPPLIER_COUNT; supplier++)
			charge_manager_update_charge(supplier, port, NULL);
	}

	board_dc_jack_handle();
}
DECLARE_HOOK(HOOK_INIT, board_charge_init, HOOK_PRIO_DEFAULT);

int board_set_active_charge_port(int port)
{
	/* charge port is a realy physical port */
	int is_real_port = (port >= 0 &&
			port < CHARGE_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = board_charger_port_is_sourcing_vbus(port);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Do not enable Type-C port if the DC Jack is present.
	 * When the Type-C is active port, hardware circuit will
	 * block DC jack from enabling +VADP_OUT.
	 */
	if (port != DC_JACK_PORT_0 && board_dc_jack_present()) {
		CPRINTS("DC Jack present, Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/* Make sure non-charging port is disabled */
	switch (port) {
	case TYPE_C_PORT_0:
		board_charging_enable(TYPE_C_PORT_1, 0);
		board_charging_enable(TYPE_C_PORT_0, 1);
		break;
	case TYPE_C_PORT_1:
		board_charging_enable(TYPE_C_PORT_0, 0);
		board_charging_enable(TYPE_C_PORT_1, 1);
		break;
	case DC_JACK_PORT_0:
	case CHARGE_PORT_NONE:
	default:
		/* Disable both Type-C ports */
		board_charging_enable(TYPE_C_PORT_0, 0);
		board_charging_enable(TYPE_C_PORT_1, 0);
		break;
	}

	return EC_SUCCESS;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
				CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

int adc_read_channel(enum adc_channel ch)
{
	return 0;
}
