/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "uart.h"
#include "usb_charge.h"
#include "util.h"

static void usb_charge_set_control_mode(int port_id, int mode)
{
	if (port_id == 0) {
		gpio_set_level(GPIO_USB1_CTL1, (mode & 0x4) >> 2);
		gpio_set_level(GPIO_USB1_CTL2, (mode & 0x2) >> 1);
		gpio_set_level(GPIO_USB1_CTL3, mode & 0x1);
	}
	else if (port_id == 1) {
		gpio_set_level(GPIO_USB2_CTL1, (mode & 0x4) >> 2);
		gpio_set_level(GPIO_USB2_CTL2, (mode & 0x2) >> 1);
		gpio_set_level(GPIO_USB2_CTL3, mode & 0x1);
	}
}


static void usb_charge_set_enabled(int port_id, int en)
{
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ENABLE, en);
	else
		gpio_set_level(GPIO_USB2_ENABLE, en);
}


static void usb_charge_set_ilim(int port_id, int sel)
{
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ILIM_SEL, sel);
	else
		gpio_set_level(GPIO_USB2_ILIM_SEL, sel);
}


int usb_charge_set_mode(int port_id, enum usb_charge_mode mode)
{
	if (port_id >= USB_CHARGE_PORT_COUNT)
		return EC_ERROR_INVAL;

	if (mode == USB_CHARGE_MODE_DISABLED) {
		usb_charge_set_enabled(port_id, 0);
		return EC_SUCCESS;
	}
	else
		usb_charge_set_enabled(port_id, 1);

	switch (mode) {
		case USB_CHARGE_MODE_CHARGE_AUTO:
			usb_charge_set_control_mode(port_id, 1);
			usb_charge_set_ilim(port_id, 1);
			break;
		case USB_CHARGE_MODE_CHARGE_BC12:
			usb_charge_set_control_mode(port_id, 4);
			break;
		case USB_CHARGE_MODE_DOWNSTREAM_500MA:
			usb_charge_set_control_mode(port_id, 2);
			usb_charge_set_ilim(port_id, 0);
			break;
		case USB_CHARGE_MODE_DOWNSTREAM_1500MA:
			usb_charge_set_control_mode(port_id, 2);
			usb_charge_set_ilim(port_id, 1);
			break;
		default:
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

static int command_set_mode(int argc, char **argv)
{
	int port_id = -1;
	int mode = -1;
	char* endptr;

	if (argc != 3) {
		uart_puts("Usage: usbchargemode <port_id> <mode>\n");
		uart_puts("Modes: 0=Disabled.\n"
			  "       1=Dedicated charging. Auto select.\n"
			  "       2=Dedicated charging. BC 1.2.\n"
			  "       3=Downstream. Max 500mA.\n"
			  "       4=Downstream. Max 1.5A.\n");
		return EC_ERROR_UNKNOWN;
	}

	port_id = strtoi(argv[1], &endptr, 0);
	if (*endptr || port_id < 0 || port_id >= USB_CHARGE_PORT_COUNT) {
		uart_puts("Invalid port ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	mode = strtoi(argv[2], &endptr, 0);
	if (*endptr || mode < 0 || mode >= USB_CHARGE_MODE_COUNT) {
		uart_puts("Invalid mode.\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("Setting USB mode...\n");
	return usb_charge_set_mode(port_id, mode);
}
DECLARE_CONSOLE_COMMAND(usbchargemode, command_set_mode);

/*****************************************************************************/
/* Initialization */

static int usb_charge_init(void)
{
	int i;

	for (i = 0; i < USB_CHARGE_PORT_COUNT; ++i)
		usb_charge_set_mode(i, USB_CHARGE_MODE_DOWNSTREAM_500MA);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, usb_charge_init, HOOK_PRIO_DEFAULT);
