/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "usb_charge.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

static uint8_t charge_mode[USB_PORT_COUNT];

static void usb_port_set_enabled(int port_id, int en)
{
	/*
	 * Only enable valid ports.
	 */
	if (usb_port_enable[port_id] >= 0) {
#ifdef CONFIG_ZEPHYR
		const struct gpio_dt_spec *gpio =
			gpio_get_dt_spec(usb_port_enable[port_id]);

		gpio_pin_set_dt(gpio, en);
#else
		gpio_or_ioex_set_level(usb_port_enable[port_id], en);
#endif
		charge_mode[port_id] = en;
	}
}

__maybe_unused static void usb_port_all_ports_on(void)
{
	int i;
	for (i = 0; i < USB_PORT_COUNT; i++)
		usb_port_set_enabled(i, 1);
}

static void usb_port_all_ports_off(void)
{
	int i;
	for (i = 0; i < USB_PORT_COUNT; i++)
		usb_port_set_enabled(i, 0);
}

/*****************************************************************************/
/* Host commands */

test_mockable int usb_charge_set_mode(int port_id, enum usb_charge_mode mode,
				      enum usb_suspend_charge inhibit_charge)
{
	CPRINTS("USB port p%d %d", port_id, mode);

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

static enum ec_status
usb_port_command_set_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_charge_set_mode *p = args->params;

	if (usb_charge_set_mode(p->usb_port_id, p->mode, p->inhibit_charge) !=
	    EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_CHARGE_SET_MODE, usb_port_command_set_mode,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

static int command_set_mode(int argc, const char **argv)
{
	int port_id = -1;
	int mode = -1;
	int i;
	char *e;

	switch (argc) {
	case 3:
		port_id = strtoi(argv[1], &e, 0);
		if (*e || port_id < 0 || port_id >= USB_PORT_COUNT)
			return EC_ERROR_PARAM1;

		if (!parse_bool(argv[2], &mode))
			return EC_ERROR_PARAM2;

		usb_port_set_enabled(port_id, mode);
		__fallthrough;
	case 1:
		for (i = 0; i < USB_PORT_COUNT; i++)
			ccprintf("Port %d: %s\n", i,
				 charge_mode[i] ? "on" : "off");
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM_COUNT;
}
DECLARE_CONSOLE_COMMAND(usbchargemode, command_set_mode, "[<port> <on | off>]",
			"Set USB charge mode");

/*****************************************************************************/
/* Hooks */

static void usb_port_preserve_state(void)
{
	system_add_jump_tag(USB_SYSJUMP_TAG, USB_HOOK_VERSION,
			    sizeof(charge_mode), charge_mode);
}
DECLARE_HOOK(HOOK_SYSJUMP, usb_port_preserve_state, HOOK_PRIO_DEFAULT);

static void usb_port_init(void)
{
	const uint8_t *prev;
	int version, size, i;

	prev = (const uint8_t *)system_get_jump_tag(USB_SYSJUMP_TAG, &version,
						    &size);
	if (!prev || version != USB_HOOK_VERSION ||
	    size != sizeof(charge_mode)) {
		usb_port_all_ports_off();
		return;
	}

	for (i = 0; i < USB_PORT_COUNT; i++)
		usb_port_set_enabled(i, prev[i]);
}
DECLARE_HOOK(HOOK_INIT, usb_port_init, HOOK_PRIO_DEFAULT);

#ifndef CONFIG_USB_PORT_POWER_DUMB_CUSTOM_HOOK
static void usb_port_startup(void)
{
	/* Turn on USB ports on as we go into S0 from S5. */
	usb_port_all_ports_on();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, usb_port_startup, HOOK_PRIO_DEFAULT);

static void usb_port_shutdown(void)
{
	/* Turn on USB ports off as we go back to S5. */
	usb_port_all_ports_off();
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_port_shutdown, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_USB_PORT_POWER_DUMB_CUSTOM_HOOK */
