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

/* We need to think about this a little more */
BUILD_ASSERT(USB_PORT_COUNT == 2);

static struct usb_state {
	uint8_t en[USB_PORT_COUNT];
	uint8_t pad[2]; /* Pad to 4 bytes for system_add_jump_tag(). */
} state;

static void usb_port_set_enabled(int port_id, int en)
{
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ENABLE, en);
	else
		gpio_set_level(GPIO_USB2_ENABLE, en);
	state.en[port_id] = en;
}

static void usb_port_all_ports_on(void)
{
	usb_port_set_enabled(0, 1);
	usb_port_set_enabled(1, 1);
}

static void usb_port_all_ports_off(void)
{
	usb_port_set_enabled(0, 0);
	usb_port_set_enabled(1, 0);
}

/*****************************************************************************/
/* Host commands */

int usb_port_set_mode(int port_id, enum usb_charge_mode mode)
{
	CPRINTF("[%T USB port p%d %d]\n", port_id, mode);

	if (port_id < 0 || port_id >= USB_PORT_COUNT)
		return EC_ERROR_INVAL;

	switch (mode) {
	case USB_CHARGE_MODE_DISABLED:
		usb_port_set_enabled(port_id, 0);
		break;
	case USB_CHARGE_MODE_ENABLED:
		usb_port_set_enabled(port_id, 1);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int usb_port_command_set_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_charge_set_mode *p = args->params;

	if (usb_port_set_mode(p->usb_port_id, p->mode) != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_CHARGE_SET_MODE,
		     usb_port_command_set_mode,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

static int command_set_mode(int argc, char **argv)
{
	int port_id = -1;
	int mode = -1;
	char *e;

	switch (argc) {
	case 3:
		port_id = strtoi(argv[1], &e, 0);
		if (*e || port_id < 0 || port_id >= USB_PORT_COUNT)
			return EC_ERROR_PARAM1;

		if (!parse_bool(argv[2], &mode))
			return EC_ERROR_PARAM2;

		usb_port_set_enabled(port_id, mode);
		/* fallthrough */
	case 1:
		ccprintf("Port 0: %s\nPort 1: %s\n",
			 state.en[0] ? "on" : "off",
			 state.en[1] ? "on" : "off");
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM_COUNT;
}
DECLARE_CONSOLE_COMMAND(usbchargemode, command_set_mode,
			"[<port> <on | off>]",
			"Set USB charge mode",
			NULL);


/*****************************************************************************/
/* Hooks */

static void usb_port_preserve_state(void)
{
	system_add_jump_tag(USB_SYSJUMP_TAG, USB_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, usb_port_preserve_state, HOOK_PRIO_DEFAULT);

static void usb_port_init(void)
{
	const struct usb_state *prev;
	int version, size;

	prev = (const struct usb_state *)system_get_jump_tag(USB_SYSJUMP_TAG,
							     &version, &size);
	if (prev && version == USB_HOOK_VERSION && size == sizeof(*prev)) {
		usb_port_set_enabled(0, prev->en[0]);
		usb_port_set_enabled(1, prev->en[1]);
	} else {
		usb_port_all_ports_off();
	}
}
DECLARE_HOOK(HOOK_INIT, usb_port_init, HOOK_PRIO_DEFAULT);

static void usb_port_resume(void)
{
	/* Turn on USB ports on as we go into S0 from S3 or S5. */
	usb_port_all_ports_on();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, usb_port_resume, HOOK_PRIO_DEFAULT);

static void usb_port_shutdown(void)
{
	/* Turn on USB ports off as we go back to S5. */
	usb_port_all_ports_off();
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_port_shutdown, HOOK_PRIO_DEFAULT);
