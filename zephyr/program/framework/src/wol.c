/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_host_command.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "flash_storage.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power_button.h"
#include "timer.h"
#include "util.h"
#include "wol.h"

#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SWITCH, format, ## args)

#define DT_DRV_COMPAT cros_ec_wake_on_lan
#define LAN_POWER_ENABLE_PIN GPIO_DT_FROM_NODE(DT_INST_PROP(0, enable_pin))
#define LAN_WAKE_SIGNAL GPIO_DT_FROM_NODE(DT_INST_PROP(0, wake_signal))
#define LAN_WAKE_SIGNAL_INTERRUPT GPIO_INT_FROM_NODE(DT_INST_PROP(0, wake_signal_irq))

/* Need to add the debounce time to avoid an EC crash if the interrupt pin is bouncing */
#define WOL_DEBOUNCE_TIME (30 * MSEC)

/* wake on lan status; true is enabled */
static bool wake_on_lan_status;

static void wake_on_lan_enable(bool enable)
{
	if (enable) {
		gpio_enable_dt_interrupt(LAN_WAKE_SIGNAL_INTERRUPT);
		/**
		 * EC storge the setup menu in the EC ROM.
		 * When EC does the initial and check the WoL enable, should turn
		 * on the lan power immediately.
		 */
		gpio_pin_set_dt(LAN_POWER_ENABLE_PIN, 1);
		wake_on_lan_status = true;
	} else {
		gpio_disable_dt_interrupt(LAN_WAKE_SIGNAL_INTERRUPT);
		wake_on_lan_status = false;
	}

	CPRINTS("Wake on LAN %sable", enable ? "en" : "dis");
}
static void wake_on_lan_init(void)
{
	int enable = flash_storage_get(FLASH_FLAGS_ENABLE_WOL);

	wake_on_lan_enable(enable);
}
DECLARE_HOOK(HOOK_INIT, wake_on_lan_init, HOOK_PRIO_DEFAULT);

static void wake_on_lan_power_on(void)
{
	bool lan_wake_satus = gpio_pin_get_dt(LAN_WAKE_SIGNAL);

	/* If chipset in shutdown mode, pulse the power button on lan to wake it. */
	if (!lan_wake_satus && chipset_in_state(CHIPSET_STATE_ANY_OFF))
		chipset_power_on();

	/* If chipset in suspend mode, set the ACPI query event to wake it. */
	if (!lan_wake_satus && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		host_set_single_event(EC_HOST_EVENT_POWER_BUTTON);
}
DECLARE_DEFERRED(wake_on_lan_power_on);

void wake_on_lan_interrupt(enum gpio_signal signal)
{
	/* debounce the wake on lan signal */
	hook_call_deferred(&wake_on_lan_power_on_data, WOL_DEBOUNCE_TIME);
}

bool wake_on_lan_is_enabled(void)
{
	return wake_on_lan_status;
}

static enum ec_status hc_wake_on_lan_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_wake_on_lan_control *p = args->params;
	struct ec_response_wake_on_lan_control *r = args->response;
	int enable = p->enable;

	if (enable == 0 || enable == 1) {
		wake_on_lan_enable(enable);

		/* Store the status in EC ROM and restore when EC power on */
		flash_storage_update(FLASH_FLAGS_ENABLE_WOL, enable);
		flash_storage_commit();
	}

	r->enable = wake_on_lan_is_enabled();
	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_WAKE_ON_LAN, hc_wake_on_lan_control, EC_VER_MASK(0));

static int cmd_wake_on_lan_control(int argc, const char **argv)
{
	int enable;
	char *e;

	if (argc == 1) {
		CPRINTS("Wake on LAN %sable", wake_on_lan_is_enabled() ? "en" : "dis");
		return EC_SUCCESS;
	}

	if (argc == 2) {
		enable = strtoi(argv[1], &e, 0);
		wake_on_lan_enable(!!enable);

		/**
		 * Don't control the LAN power in S0 state
		 * because the NIC might already be in use.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			gpio_pin_set_dt(LAN_POWER_ENABLE_PIN, !!enable);
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM2;
}
DECLARE_CONSOLE_COMMAND(wol, cmd_wake_on_lan_control,
			"[1/0]",
			"temporarily enable or disable wake on LAN until next boot");
