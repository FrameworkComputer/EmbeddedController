/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host event commands for Chrome EC */

#include "console.h"
#include "host_command.h"
#include "lpc.h"
#include "util.h"

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
#ifdef CONFIG_LPC
	lpc_set_host_events(mask);
#else
	uint32_t *mapped_raw_events =
		(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS);
	*mapped_raw_events |= mask;
#endif
}

void host_clear_events(uint32_t mask)
{
#ifdef CONFIG_LPC
	lpc_clear_host_events(mask);
#else
	uint32_t *mapped_raw_events =
		(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS);
	*mapped_raw_events &= ~mask;
#endif
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
			"[set | clear | smi | sci | wake] [mask]",
			"Print / set host event state",
			NULL);

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_LPC

static int host_event_get_smi_mask(uint8_t *data, int *resp_size)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)data;

	r->mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
	*resp_size = sizeof(struct ec_response_host_event_mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_SMI_MASK,
		     host_event_get_smi_mask);

static int host_event_get_sci_mask(uint8_t *data, int *resp_size)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)data;

	r->mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	*resp_size = sizeof(struct ec_response_host_event_mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_SCI_MASK,
		     host_event_get_sci_mask);

static int host_event_get_wake_mask(uint8_t *data, int *resp_size)
{
	struct ec_response_host_event_mask *r =
		(struct ec_response_host_event_mask *)data;

	r->mask = lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE);
	*resp_size = sizeof(struct ec_response_host_event_mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_WAKE_MASK,
		     host_event_get_wake_mask);

static int host_event_set_smi_mask(uint8_t *data, int *resp_size)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)data;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_SMI_MASK,
		     host_event_set_smi_mask);

static int host_event_set_sci_mask(uint8_t *data, int *resp_size)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)data;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_SCI_MASK,
		     host_event_set_sci_mask);

static int host_event_set_wake_mask(uint8_t *data, int *resp_size)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)data;

	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_WAKE_MASK,
		     host_event_set_wake_mask);

#endif  /* CONFIG_LPC */

static int host_event_clear(uint8_t *data, int *resp_size)
{
	const struct ec_params_host_event_mask *p =
		(const struct ec_params_host_event_mask *)data;

	host_clear_events(p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_CLEAR, host_event_clear);
