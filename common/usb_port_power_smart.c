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
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

#define USB_SYSJUMP_TAG 0x5550 /* "UP" - Usb Port */
#define USB_HOOK_VERSION 1

#ifndef CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_SDP2
#endif

/* The previous USB port state before sys jump */
struct usb_state {
	uint8_t port_mode[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT];
};

static uint8_t charge_mode[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT];

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
	ASSERT(port_id < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT);
#if CONFIG_USB_PORT_POWER_SMART_PORT_COUNT >= 1
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ENABLE, en);
#endif
#if CONFIG_USB_PORT_POWER_SMART_PORT_COUNT >= 2
	if (port_id == 1)
		gpio_set_level(GPIO_USB2_ENABLE, en);
#endif
#if CONFIG_USB_PORT_POWER_SMART_PORT_COUNT >= 3
	if (port_id == 2)
		gpio_set_level(GPIO_USB3_ENABLE, en);
#endif
}

static void usb_charge_set_ilim(int port_id, int sel)
{
#if defined(CONFIG_USB_PORT_POWER_SMART_SIMPLE)
	/* ILIM_SEL signal is shared and inverted */
	gpio_set_level(GPIO_USB_ILIM_SEL, !sel);
#elif defined(CONFIG_USB_PORT_POWER_SMART_INVERTED)
	/* ILIM_SEL signal is per-port and active low */
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ILIM_SEL_L, !sel);
	else
		gpio_set_level(GPIO_USB2_ILIM_SEL_L, !sel);
#else
	/* ILIM_SEL is per-port and active high */
	if (port_id == 0)
		gpio_set_level(GPIO_USB1_ILIM_SEL, sel);
	else
		gpio_set_level(GPIO_USB2_ILIM_SEL, sel);
#endif /* CONFIG_USB_PORT_POWER_SMART_SIMPLE */
}

static void usb_charge_all_ports_ctrl(enum usb_charge_mode mode)
{
	int i;

	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		usb_charge_set_mode(i, mode);
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
	CPRINTS("USB charge p%d m%d", port_id, mode);

	if (port_id >= CONFIG_USB_PORT_POWER_SMART_PORT_COUNT)
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
	int i;

	if (argc == 1) {
		for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
			ccprintf("Port %d: %d\n", i, charge_mode[i]);
		return EC_SUCCESS;
	}

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	port_id = strtoi(argv[1], &e, 0);
	if (*e || port_id < 0 ||
	    port_id >= CONFIG_USB_PORT_POWER_SMART_PORT_COUNT)
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
	int i;

	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		state.port_mode[i] = charge_mode[i];

	system_add_jump_tag(USB_SYSJUMP_TAG, USB_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, usb_charge_preserve_state, HOOK_PRIO_DEFAULT);

static void usb_charge_init(void)
{
	const struct usb_state *prev;
	int version, size, i;

	prev = (const struct usb_state *)system_get_jump_tag(USB_SYSJUMP_TAG,
							     &version, &size);

	if (prev && version == USB_HOOK_VERSION && size == sizeof(*prev)) {
		for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
			usb_charge_set_mode(i, prev->port_mode[i]);
	} else {
		usb_charge_all_ports_ctrl(USB_CHARGE_MODE_DISABLED);
	}
}
DECLARE_HOOK(HOOK_INIT, usb_charge_init, HOOK_PRIO_DEFAULT);

static void usb_charge_resume(void)
{
	/* Turn on USB ports on as we go into S0 from S3 or S5. */
	usb_charge_all_ports_ctrl(CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, usb_charge_resume, HOOK_PRIO_DEFAULT);

static void usb_charge_shutdown(void)
{
	/* Turn on USB ports off as we go back to S5. */
	usb_charge_all_ports_ctrl(USB_CHARGE_MODE_DISABLED);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_charge_shutdown, HOOK_PRIO_DEFAULT);
