/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "task.h"
#include "usb_mux.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <stdbool.h>

#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* eSPI device */
#define espi_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_espi))

/*******************************************************************/
/* USB-C Configuration Start */

static void usbc_interrupt_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		board_reset_pd_mcu();
	}

	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_ppc));

	/* Enable SBU fault interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_sbu_fault));
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

__override void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * Meteorlake PCH uses Virtual Wire for over current error,
	 * hence Send 'Over Current Virtual Wire' eSPI signal.
	 */
	espi_send_vwire(espi_dev, port + ESPI_VWIRE_SIGNAL_SLV_GPIO_0,
			!is_overcurrented);
}

void sbu_fault_interrupt(enum gpio_signal signal)
{
	int port = USBC_PORT_C0;

	CPRINTSUSB("C%d: SBU fault", port);
	pd_handle_overcurrent(port);
}

void reset_nct38xx_port(int port)
{
	const struct gpio_dt_spec *reset_gpio_l;
	const struct device *ioex_port0, *ioex_port1;

	/* TODO(b/225189538): Save and restore ioex signals */
	if (port == USBC_PORT_C0) {
		reset_gpio_l = &tcpc_config[0].rst_gpio;
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
	} else {
		/* Invalid port: do nothing */
		return;
	}

	gpio_pin_set_dt(reset_gpio_l, 1);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 0);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0) {
		msleep(NCT3807_RESET_POST_DELAY_MS);
	}

	/* Re-enable the IO expander pins */
	gpio_reset_port(ioex_port0);
	gpio_reset_port(ioex_port1);
}

__override bool board_is_tbt_usb4_port(int port)
{
	if (port == USBC_PORT_C0)
		return true;

	return false;
}
