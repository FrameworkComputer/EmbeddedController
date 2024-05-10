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

__override void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * Meteorlake PCH uses Virtual Wire for over current error,
	 * hence Send 'Over Current Virtual Wire' eSPI signal.
	 */
	espi_send_vwire(espi_dev, port + ESPI_VWIRE_SIGNAL_TARGET_GPIO_0,
			!is_overcurrented);
}

void sbu_fault_interrupt(enum gpio_signal signal)
{
	int port = USBC_PORT_C0;

	CPRINTSUSB("C%d: SBU fault", port);
	pd_handle_overcurrent(port);
}
