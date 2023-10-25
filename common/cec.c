/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "cec.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "power_button.h"
#include "printf.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)

#ifdef CONFIG_CEC_DEBUG
#define DEBUG_CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define DEBUG_CPRINTS(format, args...) cprints(CC_CEC, format, ##args)
#else
#define DEBUG_CPRINTF(...)
#define DEBUG_CPRINTS(...)
#endif

#define CEC_SEND_RESULTS (EC_MKBP_CEC_SEND_OK | EC_MKBP_CEC_SEND_FAILED)

/*
 * Mutex for the read-offset of the rx queue. Needed since the
 * queue is read and flushed from different contexts
 */
static mutex_t rx_queue_readoffset_mutex;

/* Queue of completed incoming CEC messages */
static struct cec_rx_queue cec_rx_queue[CEC_PORT_COUNT];

/* MKBP events to send to the AP (enum mkbp_cec_event) */
static atomic_t cec_mkbp_events[CEC_PORT_COUNT];

/* Task events for each port (CEC_TASK_EVENT_*) */
static atomic_t cec_task_events[CEC_PORT_COUNT];

int cec_transfer_get_bit(const struct cec_msg_transfer *transfer)
{
	if (transfer->byte >= MAX_CEC_MSG_LEN)
		return 0;

	return transfer->buf[transfer->byte] & (0x80 >> transfer->bit);
}

void cec_transfer_set_bit(struct cec_msg_transfer *transfer, int val)
{
	uint8_t bit_flag;

	if (transfer->byte >= MAX_CEC_MSG_LEN)
		return;
	bit_flag = 0x80 >> transfer->bit;
	transfer->buf[transfer->byte] &= ~bit_flag;
	if (val)
		transfer->buf[transfer->byte] |= bit_flag;
}

void cec_transfer_inc_bit(struct cec_msg_transfer *transfer)
{
	if (++(transfer->bit) == 8) {
		if (transfer->byte >= MAX_CEC_MSG_LEN)
			return;
		transfer->bit = 0;
		transfer->byte++;
	}
}

int cec_transfer_is_eom(const struct cec_msg_transfer *transfer, int len)
{
	if (transfer->bit)
		return 0;
	return (transfer->byte == len);
}

void cec_rx_queue_flush(struct cec_rx_queue *queue)
{
	mutex_lock(&rx_queue_readoffset_mutex);
	queue->read_offset = 0;
	mutex_unlock(&rx_queue_readoffset_mutex);
	queue->write_offset = 0;
}

struct cec_offline_policy cec_default_policy[] = {
	{
		.command = CEC_MSG_REQUEST_ACTIVE_SOURCE,
		.action = CEC_ACTION_POWER_BUTTON,
	},
	{
		.command = CEC_MSG_SET_STREAM_PATH,
		.action = CEC_ACTION_POWER_BUTTON,
	},
	/* Terminator */
	{ 0 },
};

static enum cec_action cec_find_action(const struct cec_offline_policy *policy,
				       uint8_t command)
{
	if (policy == NULL)
		return CEC_ACTION_NONE;

	while (policy->command != 0 && policy->action != 0) {
		if (policy->command == command)
			return policy->action;
		policy++;
	}

	return CEC_ACTION_NONE;
}

int cec_process_offline_message(int port, const uint8_t *msg, uint8_t msg_len)
{
	uint8_t command;
	char str_buf[hex_str_buf_size(msg_len)];

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF))
		/* Forward to the AP */
		return EC_ERROR_NOT_HANDLED;

	if (msg_len < 1)
		return EC_ERROR_INVAL;

	snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(msg, msg_len));
	DEBUG_CPRINTS("CEC%d offline msg: %s", port, str_buf);

	/* Header-only, e.g. a polling message. No command, so nothing to do */
	if (msg_len == 1)
		return EC_SUCCESS;

	command = msg[1];

	if (cec_find_action(cec_config[port].offline_policy, command) ==
	    CEC_ACTION_POWER_BUTTON)
		/* Equal to PWRBTN_INITIAL_US (for x86). */
		power_button_simulate_press(200);

	/* Consumed */
	return EC_SUCCESS;
}

int cec_rx_queue_push(struct cec_rx_queue *queue, const uint8_t *msg,
		      uint8_t msg_len)
{
	int i;
	uint32_t offset;

	if (msg_len > MAX_CEC_MSG_LEN || msg_len == 0)
		return EC_ERROR_INVAL;

	offset = queue->write_offset;
	/* Fill in message length last, if successful. Set to zero for now */
	queue->buf[offset] = 0;
	offset = (offset + 1) % CEC_RX_BUFFER_SIZE;

	for (i = 0; i < msg_len; i++) {
		if (offset == queue->read_offset) {
			/* Buffer full */
			return EC_ERROR_OVERFLOW;
		}

		queue->buf[offset] = msg[i];
		offset = (offset + 1) % CEC_RX_BUFFER_SIZE;
	}

	/*
	 * Don't commit if we caught up with read-offset
	 * since that would indicate an empty buffer
	 */
	if (offset == queue->read_offset) {
		/* Buffer full */
		return EC_ERROR_OVERFLOW;
	}

	/* Commit the push */
	queue->buf[queue->write_offset] = msg_len;
	queue->write_offset = offset;

	return EC_SUCCESS;
}

int cec_rx_queue_pop(struct cec_rx_queue *queue, uint8_t *msg, uint8_t *msg_len)
{
	int i;

	mutex_lock(&rx_queue_readoffset_mutex);
	if (queue->read_offset == queue->write_offset) {
		/* Queue empty */
		mutex_unlock(&rx_queue_readoffset_mutex);
		*msg_len = 0;
		return -1;
	}

	/* The first byte in the buffer is the message length */
	*msg_len = queue->buf[queue->read_offset];
	if (*msg_len == 0 || *msg_len > MAX_CEC_MSG_LEN) {
		mutex_unlock(&rx_queue_readoffset_mutex);
		*msg_len = 0;
		CPRINTS("CEC: Invalid msg size in queue: %u", *msg_len);
		return -1;
	}

	queue->read_offset = (queue->read_offset + 1) % CEC_RX_BUFFER_SIZE;
	for (i = 0; i < *msg_len; i++) {
		msg[i] = queue->buf[queue->read_offset];
		queue->read_offset =
			(queue->read_offset + 1) % CEC_RX_BUFFER_SIZE;
	}

	mutex_unlock(&rx_queue_readoffset_mutex);

	return 0;
}

void cec_task_set_event(int port, uint32_t event)
{
	atomic_or(&cec_task_events[port], event);
	task_wake(TASK_ID_CEC);
}

test_export_static void send_mkbp_event(int port, uint32_t event)
{
	/*
	 * We only support one transmission at a time on each port, so there
	 * should only be one send result set at a time. The host should read
	 * the send result before starting the next transmission, so this only
	 * happens if the host is misbehaving.
	 */
	if ((event & CEC_SEND_RESULTS) &&
	    (cec_mkbp_events[port] & CEC_SEND_RESULTS)) {
		CPRINTS("CEC%d warning: host did not clear send result", port);
		atomic_clear_bits(&cec_mkbp_events[port], CEC_SEND_RESULTS);
	}

	atomic_or(&cec_mkbp_events[port], event);
	mkbp_send_event(EC_MKBP_EVENT_CEC_EVENT);
}

static enum ec_status hc_cec_write(struct host_cmd_handler_args *args)
{
	int port;
	uint8_t msg_len;
	const uint8_t *msg;

	if (args->version == 0) {
		const struct ec_params_cec_write *params = args->params;

		/* v0 only supports one port, so we assume it's port 0. */
		port = 0;
		msg_len = args->params_size;
		msg = params->msg;
	} else {
		const struct ec_params_cec_write_v1 *params_v1 = args->params;

		port = params_v1->port;
		msg_len = params_v1->msg_len;
		msg = params_v1->msg;
	}

	if (port < 0 || port >= CEC_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (msg_len == 0 || msg_len > MAX_CEC_MSG_LEN)
		return EC_RES_INVALID_PARAM;

	if (cec_config[port].drv->send(port, msg, msg_len) != EC_SUCCESS)
		return EC_RES_BUSY;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CEC_WRITE_MSG, hc_cec_write,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static enum ec_status hc_cec_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_cec_read *params = args->params;
	struct ec_response_cec_read *response = args->response;
	int port = params->port;

	if (port < 0 || port >= CEC_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (cec_rx_queue_pop(&cec_rx_queue[port], response->msg,
			     &response->msg_len) != 0)
		return EC_RES_UNAVAILABLE;

	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CEC_READ_MSG, hc_cec_read, EC_VER_MASK(0));

static enum ec_status cec_set_enable(int port, uint8_t enable)
{
	if (enable != 0 && enable != 1)
		return EC_RES_INVALID_PARAM;

	if (cec_config[port].drv->set_enable(port, enable) != EC_SUCCESS)
		return EC_RES_ERROR;

	if (enable == 0) {
		/* If disabled, clear the rx queue and events. */
		memset(&cec_rx_queue[port], 0, sizeof(struct cec_rx_queue));
		cec_mkbp_events[port] = 0;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status cec_set_logical_addr(int port, uint8_t logical_addr)
{
	if (logical_addr >= CEC_BROADCAST_ADDR &&
	    logical_addr != CEC_INVALID_ADDR)
		return EC_RES_INVALID_PARAM;

	if (cec_config[port].drv->set_logical_addr(port, logical_addr) !=
	    EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status hc_cec_set(struct host_cmd_handler_args *args)
{
	const struct ec_params_cec_set *params = args->params;
	int port = params->port;

	if (port < 0 || port >= CEC_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	switch (params->cmd) {
	case CEC_CMD_ENABLE:
		return cec_set_enable(port, params->val);
	case CEC_CMD_LOGICAL_ADDRESS:
		return cec_set_logical_addr(port, params->val);
	}

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_CEC_SET, hc_cec_set, EC_VER_MASK(0));

static enum ec_status hc_cec_get(struct host_cmd_handler_args *args)
{
	struct ec_response_cec_get *response = args->response;
	const struct ec_params_cec_get *params = args->params;
	int port = params->port;

	if (port < 0 || port >= CEC_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	switch (params->cmd) {
	case CEC_CMD_ENABLE:
		if (cec_config[port].drv->get_enable(port, &response->val) !=
		    EC_SUCCESS)
			return EC_RES_ERROR;
		break;
	case CEC_CMD_LOGICAL_ADDRESS:
		if (cec_config[port].drv->get_logical_addr(
			    port, &response->val) != EC_SUCCESS)
			return EC_RES_ERROR;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CEC_GET, hc_cec_get, EC_VER_MASK(0));

static enum ec_status hc_port_count(struct host_cmd_handler_args *args)
{
	struct ec_response_cec_port_count *response = args->response;

	response->port_count = CEC_PORT_COUNT;
	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CEC_PORT_COUNT, hc_port_count, EC_VER_MASK(0));

static int cec_get_next_event(uint8_t *out)
{
	uint32_t event_out = 0;
	uint32_t events;
	int port;

	/* Find a port with pending events */
	for (port = 0; port < CEC_PORT_COUNT; port++) {
		if (!cec_mkbp_events[port])
			continue;

		events = atomic_clear(&cec_mkbp_events[port]);
		event_out = EC_MKBP_EVENT_CEC_PACK(events, port);
		break;
	}

	if (!event_out) {
		/* Didn't find any events */
		return 0;
	}

	memcpy(out, &event_out, sizeof(event_out));

	/* Notify the AP if there are more events to send */
	for (port = 0; port < CEC_PORT_COUNT; port++) {
		if (cec_mkbp_events[port]) {
			mkbp_send_event(EC_MKBP_EVENT_CEC_EVENT);
			break;
		}
	}

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_CEC_EVENT, cec_get_next_event);

static int cec_get_next_msg(uint8_t *out)
{
	int rv;
	uint8_t msg_len, msg[MAX_CEC_MSG_LEN];
	/* cec_message is only used on devices with one CEC port */
	const int port = 0;

	if (CEC_PORT_COUNT != 1) {
		CPRINTS("CEC error: cec_message used on device with %d ports",
			CEC_PORT_COUNT);
		return -1;
	}

	rv = cec_rx_queue_pop(&cec_rx_queue[port], msg, &msg_len);
	if (rv != 0)
		return -1;

	memcpy(out, msg, msg_len);

	return msg_len;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_CEC_MESSAGE, cec_get_next_msg);

static void cec_init(void)
{
	int port;

	for (port = 0; port < CEC_PORT_COUNT; port++)
		cec_config[port].drv->init(port);

	CPRINTS("CEC initialized");
}
DECLARE_HOOK(HOOK_INIT, cec_init, HOOK_PRIO_LAST);

static void handle_received_message(int port)
{
	int rv;
	uint8_t *msg;
	uint8_t msg_len;

	if (cec_config[port].drv->get_received_message(port, &msg, &msg_len) !=
	    EC_SUCCESS) {
		CPRINTS("CEC%d failed to get received message", port);
		return;
	}

	if (cec_process_offline_message(port, msg, msg_len) == EC_SUCCESS) {
		DEBUG_CPRINTS("CEC%d message consumed offline", port);
		/* Continue to queue message and notify AP. */
	}
	rv = cec_rx_queue_push(&cec_rx_queue[port], msg, msg_len);
	if (rv == EC_ERROR_OVERFLOW) {
		/* Queue full, prefer the most recent msg */
		cec_rx_queue_flush(&cec_rx_queue[port]);
		rv = cec_rx_queue_push(&cec_rx_queue[port], msg, msg_len);
	}
	if (rv != EC_SUCCESS)
		return;

	/*
	 * There are two ways of transferring received messages to the AP:
	 * 1. Old EC / kernel which only support one port send the data in a
	 *    cec_message MKBP event.
	 * 2. New EC / kernel which support multiple ports use a HAVE_DATA
	 *    event + read command.
	 * On devices with only one CEC port, the EC will continue to use
	 * cec_message for now. This allows new EC firmware to work with old
	 * kernels, which makes migration easier since it doesn't matter if the
	 * EC or kernel changes land first. This can be removed once the kernel
	 * changes to support multiple ports have landed on all relevant kernel
	 * branches.
	 */
	if (CEC_PORT_COUNT == 1)
		mkbp_send_event(EC_MKBP_EVENT_CEC_MESSAGE);
	else
		send_mkbp_event(port, EC_MKBP_CEC_HAVE_DATA);
}

void cec_task(void *unused)
{
	uint32_t events;
	int port;

	CPRINTS("CEC task starting");

	while (1) {
		task_wait_event(-1);
		for (port = 0; port < CEC_PORT_COUNT; port++) {
			events = atomic_clear(&cec_task_events[port]);
			if (events & CEC_TASK_EVENT_RECEIVED_DATA) {
				handle_received_message(port);
			}
			if (events & CEC_TASK_EVENT_OKAY) {
				send_mkbp_event(port, EC_MKBP_CEC_SEND_OK);
				DEBUG_CPRINTS("CEC%d SEND OKAY", port);
			} else if (events & CEC_TASK_EVENT_FAILED) {
				send_mkbp_event(port, EC_MKBP_CEC_SEND_FAILED);
				DEBUG_CPRINTS("CEC%d SEND FAILED", port);
			}
		}
	}
}
