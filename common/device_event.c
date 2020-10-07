/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Device event commands for Chrome EC */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "lpc.h"
#include "mkbp_event.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_EVENTS, outstr)
#define CPRINTS(format, args...) cprints(CC_EVENTS, format, ## args)

static uint32_t device_current_events;
static uint32_t device_enabled_events;

uint32_t device_get_current_events(void)
{
	return device_current_events;
}

static uint32_t device_get_and_clear_events(void)
{
	return deprecated_atomic_read_clear(&device_current_events);
}

static uint32_t device_get_enabled_events(void)
{
	return device_enabled_events;
}

void device_set_events(uint32_t mask)
{
	/* Ignore events that are not enabled */
	mask &= device_enabled_events;

	if ((device_current_events & mask) != mask)
		CPRINTS("device event set 0x%08x", mask);

	deprecated_atomic_or(&device_current_events, mask);

	/* Signal host that a device event is pending */
	host_set_single_event(EC_HOST_EVENT_DEVICE);
}

void device_clear_events(uint32_t mask)
{
	/* Only print if something's about to change */
	if (device_current_events & mask)
		CPRINTS("device event clear 0x%08x", mask);

	deprecated_atomic_clear_bits(&device_current_events, mask);
}

static void device_set_enabled_events(uint32_t mask)
{
	if ((device_enabled_events & mask) != mask)
		CPRINTS("device enabled events set 0x%08x", mask);

	device_enabled_events = mask;
}

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_DEVICE_EVENT
static int command_device_event(int argc, char **argv)
{
	/* Handle sub-commands */
	if (argc == 3) {
		char *e;
		int i = strtoi(argv[2], &e, 0);

		if (*e)
			return EC_ERROR_PARAM2;
		else if (!strcasecmp(argv[1], "set"))
			device_set_events(i);
		else if (!strcasecmp(argv[1], "clear"))
			device_clear_events(i);
		else if (!strcasecmp(argv[1], "enable"))
			device_set_enabled_events(i);
		else
			return EC_ERROR_PARAM1;
	}

	ccprintf("Enabled Events:  0x%08x\n", device_get_enabled_events());
	ccprintf("Current Events:  0x%08x\n", device_get_current_events());

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(deviceevent, command_device_event,
			"[set | clear | enable] [mask]",
			"Print / set device event state");
#endif

/*****************************************************************************/
/* Host commands */

static enum ec_status device_event_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_device_event *p = args->params;
	struct ec_response_device_event *r = args->response;

	switch (p->param) {
	case EC_DEVICE_EVENT_PARAM_GET_CURRENT_EVENTS:
		r->event_mask = device_get_and_clear_events();
		break;
	case EC_DEVICE_EVENT_PARAM_GET_ENABLED_EVENTS:
		r->event_mask = device_get_enabled_events();
		break;
	case EC_DEVICE_EVENT_PARAM_SET_ENABLED_EVENTS:
		device_set_enabled_events(p->event_mask);
		r->event_mask = device_get_enabled_events();
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_DEVICE_EVENT, device_event_cmd, EC_VER_MASK(0));
