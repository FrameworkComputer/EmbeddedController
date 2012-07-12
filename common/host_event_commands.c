/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host event commands for Chrome EC */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "lpc.h"
#include "util.h"

/* Copy B of current events mask.
 *
 * This is separate from the main copy, which affects ACPI/SCI/SMI/wake.
 *
 * Setting an event sets both copies.  Copies are cleared separately. */
static uint32_t events_copy_b;

uint32_t host_get_events(void)
{
#ifdef CONFIG_LPC
	return lpc_get_host_events();
#else
	uint32_t *mapped_raw_events =
		(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS);
	return *mapped_raw_events;
#endif
}

void host_set_events(uint32_t mask)
{
	atomic_or(&events_copy_b, mask);

#ifdef CONFIG_LPC
	lpc_set_host_events(mask);
#else
	*(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) |= mask;
#endif
}

void host_clear_events(uint32_t mask)
{
#ifdef CONFIG_LPC
	lpc_clear_host_events(mask);
#else
	*(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) &= ~mask;
#endif
}

/**
 * Clear one or more host event bits from copy B.
 *
 * @param mask          Event bits to clear (use EC_HOST_EVENT_MASK()).
 *                      Write 1 to a bit to clear it.
 */
static void host_clear_events_b(uint32_t mask)
{
	atomic_clear(&events_copy_b, mask);
}

/*****************************************************************************/
/* Console commands */

static int command_host_event(int argc, char **argv)
{
	/* Handle sub-commands */
	if (argc == 3) {
		char *e;
		int i = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (!strcasecmp(argv[1], "set"))
			host_set_events(i);
		else if (!strcasecmp(argv[1], "clear"))
			host_clear_events(i);
		else if (!strcasecmp(argv[1], "clearb"))
			host_clear_events_b(i);
#ifdef CONFIG_LPC
		else if (!strcasecmp(argv[1], "smi"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, i);
		else if (!strcasecmp(argv[1], "sci"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, i);
		else if (!strcasecmp(argv[1], "wake"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, i);
#endif
		else
			return EC_ERROR_PARAM1;
	}

	/* Print current SMI/SCI status */
	ccprintf("Events:    0x%08x\n", host_get_events());
	ccprintf("Events-B:  0x%08x\n", events_copy_b);
#ifdef CONFIG_LPC
	ccprintf("SMI mask:  0x%08x\n",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_SMI));
	ccprintf("SCI mask:  0x%08x\n",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_SCI));
	ccprintf("Wake mask: 0x%08x\n",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE));
#endif
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hostevent, command_host_event,
			"[set | clear | clearb | smi | sci | wake] [mask]",
			"Print / set host event state",
			NULL);

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_LPC

static int host_event_get_smi_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)args->response;

	r->mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_SMI_MASK,
		     host_event_get_smi_mask,
		     EC_VER_MASK(0));

static int host_event_get_sci_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)args->response;

	r->mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_SCI_MASK,
		     host_event_get_sci_mask,
		     EC_VER_MASK(0));

static int host_event_get_wake_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)args->response;

	r->mask = lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_WAKE_MASK,
		     host_event_get_wake_mask,
		     EC_VER_MASK(0));

static int host_event_set_smi_mask(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)args->params;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_SMI_MASK,
		     host_event_set_smi_mask,
		     EC_VER_MASK(0));

static int host_event_set_sci_mask(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)args->params;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_SCI_MASK,
		     host_event_set_sci_mask,
		     EC_VER_MASK(0));

static int host_event_set_wake_mask(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)args->params;

	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_WAKE_MASK,
		     host_event_set_wake_mask,
		     EC_VER_MASK(0));

#endif  /* CONFIG_LPC */

static int host_event_get_b(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)args->response;

	r->mask = events_copy_b;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_B,
		     host_event_get_b,
		     EC_VER_MASK(0));

static int host_event_clear(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)args->params;

	host_clear_events(p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_CLEAR,
		     host_event_clear,
		     EC_VER_MASK(0));

static int host_event_clear_b(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)args->params;

	host_clear_events_b(p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_CLEAR_B,
		     host_event_clear_b,
		     EC_VER_MASK(0));
