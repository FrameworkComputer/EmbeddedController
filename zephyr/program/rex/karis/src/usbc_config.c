/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "driver/ppc/ktu1125_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "usb_mux_config.h"
#include "usbc_config.h"

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	if (usb_db_type == FW_USB_DB_USB3 && tcpc_config[1].rst_gpio.port) {
		gpio_pin_set_dt(&tcpc_config[1].rst_gpio, 1);
		msleep(PS8XXX_RESET_DELAY_MS);
		gpio_pin_set_dt(&tcpc_config[1].rst_gpio, 0);
		msleep(PS8815_FW_INIT_DELAY_MS);
	}
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		switch (usb_db_type) {
		case FW_USB_DB_USB3:
			nx20p348x_interrupt(USBC_PORT_C1);
			break;
		case FW_USB_DB_USB4_ANX7452:
		case FW_USB_DB_USB4_ANX7452_V2:
			syv682x_interrupt(USBC_PORT_C1);
			break;
		case FW_USB_DB_USB4_KB8010:
			ktu1125_interrupt(USBC_PORT_C1);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

__override bool board_is_tbt_usb4_port(int port)
{
	if (port == USBC_PORT_C0)
		return true;
	if (port == USBC_PORT_C1 && usb_db_type != FW_USB_DB_USB3)
		return true;

	return false;
}
