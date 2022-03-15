/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "bc12/pi3usb9201_public.h"
#include "charge_ramp.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "charger/isl9241_public.h"
#include "config.h"
#include "fff.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c/i2c.h"
#include "power.h"
#include "ppc/sn5s330_public.h"
#include "ppc/syv682x_public.h"
#include "retimer/bb_retimer_public.h"
#include "stubs.h"
#include "tcpm/ps8xxx_public.h"
#include "tcpm/tcpci.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "charge_state_v2.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(stubs);

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* All of these definitions are just to get the test to link. None of these
 * functions are useful or behave as they should. Please remove them once the
 * real code is able to be added.  Most of the things here should either be
 * in emulators or in the native_posix board-specific code or part of the
 * device tree.
 */

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_1_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

/* Charger Chip Configuration */
const struct charger_config_t chg_chips[] = {
#ifdef CONFIG_PLATFORM_EC_CHARGER_ISL9238
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
#endif
};

uint8_t board_get_charger_chip_count(void)
{
	return ARRAY_SIZE(chg_chips);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_MAX_COUNT);
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

int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	charge_set_input_current_limit(
	MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = DT_REG_ADDR(DT_NODELABEL(tcpci_emul)),
		},
		.drv = &tcpci_tcpm_drv,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1,
			.addr_flags = DT_REG_ADDR(DT_NODELABEL(
							tcpci_ps8xxx_emul)),
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

static uint16_t ps8xxx_product_id = PS8805_PRODUCT_ID;

uint16_t board_get_ps8xxx_product_id(int port)
{
	if (port != USBC_PORT_C1) {
		return 0;
	}

	return ps8xxx_product_id;
}

void board_set_ps8xxx_product_id(uint16_t product_id)
{
	ps8xxx_product_id = product_id;
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

struct usb_mux usbc0_virtual_usb_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

struct usb_mux usbc1_virtual_usb_mux = {
	.usb_port = USBC_PORT_C1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.next_mux = &usbc0_virtual_usb_mux,
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = DT_REG_ADDR(DT_NODELABEL(tcpci_emul)),
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.driver = &bb_usb_retimer,
		.hpd_update = bb_retimer_hpd_update,
		.next_mux = &usbc1_virtual_usb_mux,
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = DT_REG_ADDR(DT_NODELABEL(
					usb_c1_bb_retimer_emul)),
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

struct bb_usb_control bb_controls[] = {
	[USBC_PORT_C0] = {
		/* USB-C port 0 doesn't have a retimer */
	},
	[USBC_PORT_C1] = {
		.usb_ls_en_gpio = GPIO_SIGNAL(DT_NODELABEL(usb_c1_ls_en)),
		.retimer_rst_gpio =
			 GPIO_SIGNAL(DT_NODELABEL(usb_c1_rt_rst_odl)),
	},
};
BUILD_ASSERT(ARRAY_SIZE(bb_controls) == USBC_PORT_COUNT);

void pd_power_supply_reset(int port)
{
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = SYV682X_ADDR1_FLAGS,
		.frs_en = GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c1_frs_en)),
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

DEFINE_FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_int_odl))) {
		if (gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_rst_l)) != 0)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_int_odl))) {
		if (gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_rst_l)) != 0)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	int port;

	switch (signal) {
	case GPIO_SIGNAL(DT_NODELABEL(usb_c0_tcpc_int_odl)):
		port = 0;
		break;
	case GPIO_SIGNAL(DT_NODELABEL(usb_c1_tcpc_int_odl)):
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void ppc_alert(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_ppc_int)):
		ppc_chips[USBC_PORT_C0].drv->interrupt(USBC_PORT_C0);
		break;
	case GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c1_ppc_int)):
		ppc_chips[USBC_PORT_C1].drv->interrupt(USBC_PORT_C1);
		break;
	default:
		return;
	}
}

/* TODO: This code should really be generic, and run based on something in
 * the dts.
 */
static void stubs_interrupt_init(void)
{
	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));

	cprints(CC_USB, "Resetting TCPCs...");
	cflush();

	/* Reset generic TCPCI on port 0. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_rst_l), 0);
	msleep(1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_rst_l), 1);

	/* Reset PS8XXX on port 1. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_rst_l), 0);
	msleep(PS8XXX_RESET_DELAY_MS);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_rst_l), 1);

	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_ppc));

	/* Enable SwitchCap interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_switchcap_pg));
}
DECLARE_HOOK(HOOK_INIT, stubs_interrupt_init, HOOK_PRIO_INIT_I2C + 1);

void board_set_switchcap_power(int enable)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_switchcap_on), enable);
	/* TODO(b/217554681): So, the ln9310 emul should probably be setting
	 * this instead of setting it here.
	 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_src_vph_pwr_pg), enable);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mb_power_good), enable);
}

int board_is_switchcap_enabled(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_switchcap_on));
}

int board_is_switchcap_power_good(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_src_vph_pwr_pg));
}

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);
}

/* GPIO TEST interrupt handler */
bool gpio_test_interrupt_triggered;
void gpio_test_interrupt(enum gpio_signal signal)
{
	ARG_UNUSED(signal);
	printk("%s called\n", __func__);
	gpio_test_interrupt_triggered = true;
}

int clock_get_freq(void)
{
	return 16000000;
}
