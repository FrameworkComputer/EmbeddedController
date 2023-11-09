/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

#include "builtin/assert.h"
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
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

#ifndef CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_SDP2
#endif

struct charge_mode_t {
	uint8_t mode : 7;
	uint8_t inhibit_charging_in_suspend : 1;
} __pack;

static struct charge_mode_t charge_mode[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT];

#ifdef CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
/*
 * If we only support CDP and SDP, the control signals are hard-wired so
 * there's nothing to do.  The only to do is set ILIM_SEL.
 */
static void usb_charge_set_control_mode(int port_id, int mode)
{
}
#else /* !defined(CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY) */
static void usb_charge_set_control_mode(int port_id, int mode)
{
#ifdef CONFIG_USB_PORT_POWER_SMART_SIMPLE
	/*
	 * One single shared control signal, so the last mode set to either
	 * port wins.  Also, only CTL1 can be set; the other pins are
	 * hard-wired.
	 */
	gpio_or_ioex_set_level(GPIO_USB_CTL1, mode & 0x4);
#else
	if (port_id == 0) {
		gpio_or_ioex_set_level(GPIO_USB1_CTL1, mode & 0x4);
		gpio_or_ioex_set_level(GPIO_USB1_CTL2, mode & 0x2);
		gpio_or_ioex_set_level(GPIO_USB1_CTL3, mode & 0x1);
	} else {
		gpio_or_ioex_set_level(GPIO_USB2_CTL1, mode & 0x4);
		gpio_or_ioex_set_level(GPIO_USB2_CTL2, mode & 0x2);
		gpio_or_ioex_set_level(GPIO_USB2_CTL3, mode & 0x1);
	}
#endif /* defined(CONFIG_USB_PORT_POWER_SMART_SIMPLE) */
}
#endif /* defined(CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY) */

static void usb_charge_set_enabled(int port_id, int en)
{
	ASSERT(port_id < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT);
	/*
	 * Only enable valid ports.
	 */
	if (usb_port_enable[port_id] >= 0) {
		gpio_or_ioex_set_level(usb_port_enable[port_id], en);
	}
}

static void usb_charge_set_ilim(int port_id, int sel)
{
	int ilim_sel;

#if defined(CONFIG_USB_PORT_POWER_SMART_SIMPLE) || \
	defined(CONFIG_USB_PORT_POWER_SMART_INVERTED)
	/* ILIM_SEL is inverted. */
	sel = !sel;
#endif

	ilim_sel = GPIO_USB1_ILIM_SEL;
#if !defined(CONFIG_USB_PORT_POWER_SMART_SIMPLE) && \
	CONFIG_USB_PORT_POWER_SMART_PORT_COUNT == 2
	if (port_id != 0)
		ilim_sel = GPIO_USB2_ILIM_SEL;
#endif

	gpio_or_ioex_set_level(ilim_sel, sel);
}

static void usb_charge_all_ports_ctrl(enum usb_charge_mode mode)
{
	int i;

	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		usb_charge_set_mode(i, mode, USB_ALLOW_SUSPEND_CHARGE);
}

test_mockable int usb_charge_set_mode(int port_id, enum usb_charge_mode mode,
				      enum usb_suspend_charge inhibit_charge)
{
	CPRINTS("USB charge p%d m%d i%d", port_id, mode, inhibit_charge);

	if (port_id >= CONFIG_USB_PORT_POWER_SMART_PORT_COUNT)
		return EC_ERROR_INVAL;

	if (mode == USB_CHARGE_MODE_DEFAULT)
		mode = CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE;

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
#ifndef CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
	case USB_CHARGE_MODE_DCP_SHORT:
		usb_charge_set_control_mode(port_id, 4);
		usb_charge_set_enabled(port_id, 1);
		break;
#endif /* !defined(CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY) */
	default:
		return EC_ERROR_UNKNOWN;
	}

	charge_mode[port_id].mode = mode;
	charge_mode[port_id].inhibit_charging_in_suspend = inhibit_charge;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

static int command_set_mode(int argc, const char **argv)
{
	int port_id = -1;
	int mode = -1, inhibit_charge = 0;
	char *e;
	int i;

	if (argc == 1) {
		for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
			ccprintf("Port %d: %d,%d\n", i, charge_mode[i].mode,
				 charge_mode[i].inhibit_charging_in_suspend);
		return EC_SUCCESS;
	}

	if (argc != 3 && argc != 4)
		return EC_ERROR_PARAM_COUNT;

	port_id = strtoi(argv[1], &e, 0);
	if (*e || port_id < 0 ||
	    port_id >= CONFIG_USB_PORT_POWER_SMART_PORT_COUNT)
		return EC_ERROR_PARAM1;

	mode = strtoi(argv[2], &e, 0);
	if (*e || mode < 0 || mode >= USB_CHARGE_MODE_COUNT)
		return EC_ERROR_PARAM2;

	if (argc == 4) {
		inhibit_charge = strtoi(argv[3], &e, 0);
		if (*e || (inhibit_charge != 0 && inhibit_charge != 1))
			return EC_ERROR_PARAM3;
	}

	return usb_charge_set_mode(port_id, mode, inhibit_charge);
}
DECLARE_CONSOLE_COMMAND(usbchargemode, command_set_mode,
			"[<port> <0 | 1 | 2 | 3> [<0 | 1>]]",
			"Set USB charge mode");

/*****************************************************************************/
/* Host commands */

static enum ec_status
usb_charge_command_set_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_charge_set_mode *p = args->params;

	if (usb_charge_set_mode(p->usb_port_id, p->mode, p->inhibit_charge) !=
	    EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_CHARGE_SET_MODE, usb_charge_command_set_mode,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Hooks */

static void usb_charge_preserve_state(void)
{
	system_add_jump_tag(USB_SYSJUMP_TAG, USB_HOOK_VERSION,
			    sizeof(charge_mode), charge_mode);
}
DECLARE_HOOK(HOOK_SYSJUMP, usb_charge_preserve_state, HOOK_PRIO_DEFAULT);

static void usb_charge_init(void)
{
	const struct charge_mode_t *prev;
	int version, size, i;

	prev = (const struct charge_mode_t *)system_get_jump_tag(
		USB_SYSJUMP_TAG, &version, &size);

	if (!prev || version != USB_HOOK_VERSION ||
	    size != sizeof(charge_mode)) {
		usb_charge_all_ports_ctrl(USB_CHARGE_MODE_DISABLED);
		return;
	}

	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		usb_charge_set_mode(i, prev[i].mode,
				    prev[i].inhibit_charging_in_suspend);
}
DECLARE_HOOK(HOOK_INIT, usb_charge_init, HOOK_PRIO_DEFAULT);

static void usb_charge_resume(void)
{
	int i;

	/* Turn on USB ports on as we go into S0 from S3 or S5. */
	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		usb_charge_set_mode(i, CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE,
				    charge_mode[i].inhibit_charging_in_suspend);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, usb_charge_resume, HOOK_PRIO_DEFAULT);

static void usb_charge_suspend(void)
{
	int i;

	/*
	 * Inhibit charging during suspend if the inhibit_charging_in_suspend
	 * is set to 1.
	 */
	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		if (charge_mode[i].inhibit_charging_in_suspend)
			usb_charge_set_enabled(i, 0 /* disabled */);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, usb_charge_suspend, HOOK_PRIO_DEFAULT);

static void usb_charge_shutdown(void)
{
	/* Turn on USB ports off as we go back to S5. */
	usb_charge_all_ports_ctrl(USB_CHARGE_MODE_DISABLED);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_charge_shutdown, HOOK_PRIO_DEFAULT);
