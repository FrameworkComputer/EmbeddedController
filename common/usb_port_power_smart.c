/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "usb_charge.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define USB_SYSJUMP_TAG 0x5550 /* "UP" - Usb Port */
#define USB_HOOK_VERSION 1

#define USB_CHARGE_PORT_COUNT 2

/* The previous USB port state before sys jump */
struct usb_state {
	uint8_t port_mode[USB_CHARGE_PORT_COUNT];
};

static uint8_t charge_mode[USB_CHARGE_PORT_COUNT];

static void usb_charge_set_control_mode(int port_id, int mode)
{
#ifdef CONFIG_USB_PORT_POWER_SMART_SIMPLE
	/*
	 * One single shared control signal, so the last mode set to either
	 * port wins.  Also, only CTL1 can be set; the other pins are
	 * hard-wired.
	 */
	gpio_set_level(GPIO_USB_CTL1, mode & 0x4);
#else
	if (port_id == 0) {
		gpio_set_level(GPIO_USB1_CTL1, mode & 0x4);
		gpio_set_level(GPIO_USB1_CTL2, mode & 0x2);
		gpio_set_level(GPIO_USB1_CTL3, mode & 0x1);
	} else {
		gpio_set_level(GPIO_USB2_CTL1, mode & 0x4);
		gpio_set_level(GPIO_USB2_CTL2, mode & 0x2);
		gpio_set_level(GPIO_USB2_CTL3, mode & 0x1);
	}
#endif
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
#ifdef CONFIG_USB_PORT_POWER_SMART_SIMPLE
	/* ILIM_SEL signal is shared too */
	gpio_set_level(GPIO_USB_ILIM_SEL, sel);
#else
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ILIM_SEL, sel);
	else
		gpio_set_level(GPIO_USB2_ILIM_SEL, sel);
#endif
}

static void usb_charge_all_ports_on(void)
{
	usb_charge_set_mode(0, USB_CHARGE_MODE_SDP2);
	usb_charge_set_mode(1, USB_CHARGE_MODE_SDP2);
}

static void usb_charge_all_ports_off(void)
{
	usb_charge_set_mode(0, USB_CHARGE_MODE_DISABLED);
	usb_charge_set_mode(1, USB_CHARGE_MODE_DISABLED);
}

int usb_charge_ports_enabled(void)
{
	int mask = 0;

	if (gpio_get_level(GPIO_USB1_ENABLE))
		mask |= (1 << 0);

	if (gpio_get_level(GPIO_USB2_ENABLE))
		mask |= (1 << 1);

	return mask;
}

int usb_charge_set_mode(int port_id, enum usb_charge_mode mode)
{
	CPRINTF("[%T USB charge p%d m%d]\n", port_id, mode);

	if (port_id >= USB_CHARGE_PORT_COUNT)
		return EC_ERROR_INVAL;

	switch (mode) {
	case USB_CHARGE_MODE_DISABLED:
		usb_charge_set_enabled(port_id, 0);
		break;
	case USB_CHARGE_MODE_SDP2:
		usb_charge_set_control_mode(port_id, 7);
		usb_charge_set_ilim(port_id, 0);
		usb_charge_set_enabled(port_id, 1);
		break;
	case USB_CHARGE_MODE_CDP:
		usb_charge_set_control_mode(port_id, 7);
		usb_charge_set_ilim(port_id, 1);
		usb_charge_set_enabled(port_id, 1);
		break;
	case USB_CHARGE_MODE_DCP_SHORT:
		usb_charge_set_control_mode(port_id, 4);
		usb_charge_set_enabled(port_id, 1);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	charge_mode[port_id] = mode;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

static int command_set_mode(int argc, char **argv)
{
	int port_id = -1;
	int mode = -1;
	char *e;

	if (argc == 1) {
		ccprintf("Port 0: %d\nPort 1: %d\n",
			 charge_mode[0], charge_mode[1]);
		return EC_SUCCESS;
	}

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	port_id = strtoi(argv[1], &e, 0);
	if (*e || port_id < 0 || port_id >= USB_CHARGE_PORT_COUNT)
		return EC_ERROR_PARAM1;

	mode = strtoi(argv[2], &e, 0);
	if (*e || mode < 0 || mode >= USB_CHARGE_MODE_COUNT)
		return EC_ERROR_PARAM2;

	return usb_charge_set_mode(port_id, mode);
}
DECLARE_CONSOLE_COMMAND(usbchargemode, command_set_mode,
			"[<port> <0 | 1 | 2 | 3>]",
			"Set USB charge mode",
			"Modes: 0=Disabled.\n"
			"       1=Standard downstream port.\n"
			"	2=Charging downstream port, BC 1.2.\n"
			"       3=Dedicated charging port, BC 1.2.\n");

/*****************************************************************************/
/* Host commands */

static int usb_charge_command_set_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_charge_set_mode *p = args->params;

	if (usb_charge_set_mode(p->usb_port_id, p->mode) != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_CHARGE_SET_MODE,
		     usb_charge_command_set_mode,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Hooks */

static void usb_charge_preserve_state(void)
{
	struct usb_state state;

	state.port_mode[0] = charge_mode[0];
	state.port_mode[1] = charge_mode[1];

	system_add_jump_tag(USB_SYSJUMP_TAG, USB_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, usb_charge_preserve_state, HOOK_PRIO_DEFAULT);

static void usb_charge_init(void)
{
	const struct usb_state *prev;
	int version, size;

	prev = (const struct usb_state *)system_get_jump_tag(USB_SYSJUMP_TAG,
							     &version, &size);
	if (prev && version == USB_HOOK_VERSION && size == sizeof(*prev)) {
		usb_charge_set_mode(0, prev->port_mode[0]);
		usb_charge_set_mode(1, prev->port_mode[1]);
	} else {
		usb_charge_all_ports_off();
	}
}
DECLARE_HOOK(HOOK_INIT, usb_charge_init, HOOK_PRIO_DEFAULT);

static void usb_charge_resume(void)
{
	/* Turn on USB ports on as we go into S0 from S3 or S5. */
	usb_charge_all_ports_on();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, usb_charge_resume, HOOK_PRIO_DEFAULT);

static void usb_charge_shutdown(void)
{
	/* Turn on USB ports off as we go back to S5. */
	usb_charge_all_ports_off();
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_charge_shutdown, HOOK_PRIO_DEFAULT);
