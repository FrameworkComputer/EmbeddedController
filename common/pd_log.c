/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "charge_manager.h"
#include "console.h"
#include "event_log.h"
#include "host_command.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

/*
 * Ensure PD logging parameters are compatible with the generic logging
 * framework that we're calling into.
 */
BUILD_ASSERT(sizeof(struct ec_response_pd_log) ==
	     sizeof(struct event_log_entry));
BUILD_ASSERT(PD_LOG_SIZE_MASK == EVENT_LOG_SIZE_MASK);
BUILD_ASSERT(PD_LOG_TIMESTAMP_SHIFT == EVENT_LOG_TIMESTAMP_SHIFT);
BUILD_ASSERT(PD_EVENT_NO_ENTRY == EVENT_LOG_NO_ENTRY);

void pd_log_event(uint8_t type, uint8_t size_port, uint16_t data, void *payload)
{
	uint32_t timestamp = get_time().val >> PD_LOG_TIMESTAMP_SHIFT;

	log_add_event(type, size_port, data, payload, timestamp);
}

#ifdef HAS_TASK_HOSTCMD

/* number of accessory entries we have queued since last check */
static volatile int incoming_logs;

void pd_log_recv_vdm(int port, int cnt, uint32_t *payload)
{
	struct ec_response_pd_log *r = (void *)&payload[1];
	/* update port number from MCU point of view */
	size_t size = PD_LOG_SIZE(r->size_port);
	uint8_t size_port = PD_LOG_PORT_SIZE(port, size);
	uint32_t timestamp;

	if ((cnt < 2 + DIV_ROUND_UP(size, sizeof(uint32_t))) ||
	    !(payload[0] & VDO_SRC_RESPONDER))
		/* Not a proper log entry, bail out */
		return;

	if (r->type != PD_EVENT_NO_ENTRY) {
		timestamp = (get_time().val >> PD_LOG_TIMESTAMP_SHIFT) -
			    r->timestamp;
		log_add_event(r->type, size_port, r->data, r->payload,
			      timestamp);
		/* record that we have enqueued new content */
		incoming_logs++;
	}
}

/* we are a PD MCU/EC, send back the events to the host */
static enum ec_status hc_pd_get_log_entry(struct host_cmd_handler_args *args)
{
	struct ec_response_pd_log *r = args->response;

dequeue_retry:
	args->response_size = log_dequeue_event((struct event_log_entry *)r);
	/* if the MCU log no longer has entries, try connected accessories */
	if (r->type == PD_EVENT_NO_ENTRY) {
		int i, res;
		incoming_logs = 0;
		for (i = 0; i < board_get_usb_pd_port_count(); ++i) {
			/* only accessories who knows Google logging format */
			if (pd_get_identity_vid(i) != USB_VID_GOOGLE)
				continue;
			res = pd_fetch_acc_log_entry(i);
			if (res == EC_RES_BUSY) /* host should retry */
				return EC_RES_BUSY;
		}
		/* we have received new entries from an accessory */
		if (incoming_logs)
			goto dequeue_retry;
		/* else the current entry is already "PD_EVENT_NO_ENTRY" */
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_GET_LOG_ENTRY, hc_pd_get_log_entry,
		     EC_VER_MASK(0));

static enum ec_status hc_pd_write_log_entry(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_write_log_entry *p = args->params;
	uint8_t type = p->type;
	uint8_t port = p->port;

	if (type < PD_EVENT_MCU_BASE || type >= PD_EVENT_ACC_BASE)
		return EC_RES_INVALID_PARAM;
	if (port > 0 && port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	switch (type) {
	/* Charge event: Log data for all ports */
	case PD_EVENT_MCU_CHARGE:
#ifdef CONFIG_CHARGE_MANAGER
		charge_manager_save_log(port);
#endif
		break;

	/* Other events: no extra data, just log event type + port */
	case PD_EVENT_MCU_CONNECT:
	case PD_EVENT_MCU_BOARD_CUSTOM:
	default:
		pd_log_event(type, PD_LOG_PORT_SIZE(port, 0), 0, NULL);
		break;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_WRITE_LOG_ENTRY, hc_pd_write_log_entry,
		     EC_VER_MASK(0));
#else /* !HAS_TASK_HOSTCMD */
/* we are a PD accessory, send back the events as a VDM (VDO_CMD_GET_LOG) */
int pd_vdm_get_log_entry(uint32_t *payload)
{
	struct ec_response_pd_log *r = (void *)&payload[1];
	int byte_size;

	byte_size = log_dequeue_event((struct event_log_entry *)r);

	return 1 + DIV_ROUND_UP(byte_size, sizeof(uint32_t));
}
#endif /* !HAS_TASK_HOSTCMD */
