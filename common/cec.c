/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "chipset.h"
#include "console.h"
#include "mkbp_event.h"
#include "power_button.h"
#include "printf.h"
#include "task.h"

#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)

/*
 * Mutex for the read-offset of the rx queue. Needed since the
 * queue is read and flushed from different contexts
 */
static struct mutex rx_queue_readoffset_mutex;

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
		.command = CEC_MSG_IMAGE_VIEW_ON,
		.action = CEC_ACTION_POWER_BUTTON,
	},
	{
		.command = CEC_MSG_TEXT_VIEW_ON,
		.action = CEC_ACTION_POWER_BUTTON,
	},
	/* Terminator */
	{ 0 },
};

__overridable const struct cec_config_t cec_config = {};

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

int cec_process_offline_message(struct cec_rx_queue *queue, const uint8_t *msg,
				uint8_t msg_len)
{
	uint8_t command;
	char str_buf[hex_str_buf_size(msg_len)];

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF))
		/* Forward to the AP */
		return EC_ERROR_NOT_HANDLED;

	if (msg_len < 1)
		return EC_ERROR_INVAL;

	snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(msg, msg_len));
	CPRINTS("MSG: %s", str_buf);

	command = msg[1];

	if (cec_find_action(cec_config.offline_policy, command) ==
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
		CPRINTF("Invalid CEC msg size: %u\n", *msg_len);
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
