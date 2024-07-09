/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "bc12/pi3usb9201_public.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "charger/isl9241_public.h"
#include "config.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c/i2c.h"
#include "power.h"
#include "ppc/sn5s330_public.h"
#include "ppc/syv682x_public.h"
#include "retimer/bb_retimer_public.h"
#include "tcpm/ps8xxx_public.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stubs);

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* All of these definitions are just to get the test to link. None of these
 * functions are useful or behave as they should. Please remove them once the
 * real code is able to be added.  Most of the things here should either be
 * in emulators or in the native_sim board-specific code or part of the
 * device tree.
 */

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

int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return 0;
}

BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

static uint16_t ps8xxx_product_id = PS8805_PRODUCT_ID;

uint16_t board_get_ps8xxx_product_id(int port)
{
	if (tcpc_config[port].drv == &ps8xxx_tcpm_drv) {
		return ps8xxx_product_id;
	}

	return 0;
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

void pd_power_supply_reset(int port)
{
}

int pd_check_vconn_swap(int port)
{
	return !chipset_in_state(CHIPSET_STATE_HARD_OFF);
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

DEFINE_FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);

DEFINE_FAKE_VOID_FUNC(board_reset_pd_mcu);

#ifndef CONFIG_PLATFORM_EC_TCPC_INTERRUPT
uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_int_odl))) {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_rst_l)) == 0)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_int_odl))) {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_rst_l)) == 0)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}
#endif

/* TODO: This code should really be generic, and run based on something in
 * the dts.
 */
static void stubs_interrupt_init(void)
{
	cprints(CC_USB, "Resetting TCPCs...");
	cflush();

#if !(CONFIG_PLATFORM_EC_TCPC_INTERRUPT)
	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));
#endif

	/* Reset generic TCPCI on port 0. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_rst_l), 1);
	crec_msleep(1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_tcpc_rst_l), 0);

	/* Reset PS8XXX on port 1. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_rst_l), 1);
	crec_msleep(PS8XXX_RESET_DELAY_MS);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c1_tcpc_rst_l), 0);

	/* Enable SwitchCap interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_switchcap_pg));
}
DECLARE_HOOK(HOOK_INIT, stubs_interrupt_init, HOOK_PRIO_POST_I2C);

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
