/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "console.h"
#include "task.h"

#define CPRINTF(format, args...) cprintf(CC_CEC, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ## args)

/*
 * Mutex for the read-offset of the circular buffer. Needed since the
 * buffer is read and flushed from different contexts
 */
static struct mutex circbuf_readoffset_mutex;

int msgt_get_bit(const struct cec_msg_transfer *msgt)
{
	if (msgt->byte >= MAX_CEC_MSG_LEN)
		return 0;

	return msgt->buf[msgt->byte] & (0x80 >> msgt->bit);
}

void msgt_set_bit(struct cec_msg_transfer *msgt, int val)
{
	uint8_t bit_flag;

	if (msgt->byte >= MAX_CEC_MSG_LEN)
		return;
	bit_flag = 0x80 >> msgt->bit;
	msgt->buf[msgt->byte] &= ~bit_flag;
	if (val)
		msgt->buf[msgt->byte] |= bit_flag;
}

void msgt_inc_bit(struct cec_msg_transfer *msgt)
{
	if (++(msgt->bit) == 8) {
		if (msgt->byte >= MAX_CEC_MSG_LEN)
			return;
		msgt->bit = 0;
		msgt->byte++;
	}
}

int msgt_is_eom(const struct cec_msg_transfer *msgt, int len)
{
	if (msgt->bit)
		return 0;
	return (msgt->byte == len);
}

void rx_circbuf_flush(struct cec_rx_cb *cb)
{
	mutex_lock(&circbuf_readoffset_mutex);
	cb->read_offset = 0;
	mutex_unlock(&circbuf_readoffset_mutex);
	cb->write_offset = 0;
}

int rx_circbuf_push(struct cec_rx_cb *cb, uint8_t *msg, uint8_t msg_len)
{
	int i;
	uint32_t offset;

	if (msg_len > MAX_CEC_MSG_LEN || msg_len == 0)
		return EC_ERROR_INVAL;

	offset = cb->write_offset;
	/* Fill in message length last, if successful. Set to zero for now */
	cb->buf[offset] = 0;
	offset = (offset + 1) % CEC_CIRCBUF_SIZE;

	for (i = 0 ; i < msg_len; i++) {
		if (offset == cb->read_offset) {
			/* Buffer full */
			return EC_ERROR_OVERFLOW;
		}

		cb->buf[offset] = msg[i];
		offset = (offset + 1) % CEC_CIRCBUF_SIZE;
	}

	/*
	 * Don't commit if we caught up with read-offset
	 * since that would indicate an empty buffer
	 */
	if (offset == cb->read_offset) {
		/* Buffer full */
		return EC_ERROR_OVERFLOW;
	}

	/* Commit the push */
	cb->buf[cb->write_offset] = msg_len;
	cb->write_offset = offset;

	return EC_SUCCESS;
}

int rx_circbuf_pop(struct cec_rx_cb *cb, uint8_t *msg, uint8_t *msg_len)
{
	int i;

	mutex_lock(&circbuf_readoffset_mutex);
	if (cb->read_offset == cb->write_offset) {
		/* Circular buffer empty */
		mutex_unlock(&circbuf_readoffset_mutex);
		*msg_len = 0;
		return -1;
	}

	/* The first byte in the buffer is the message length */
	*msg_len = cb->buf[cb->read_offset];
	if (*msg_len == 0 || *msg_len > MAX_CEC_MSG_LEN) {
		mutex_unlock(&circbuf_readoffset_mutex);
		*msg_len = 0;
		CPRINTF("Invalid CEC msg size: %u\n", *msg_len);
		return -1;
	}

	cb->read_offset = (cb->read_offset + 1) % CEC_CIRCBUF_SIZE;
	for (i = 0; i < *msg_len; i++) {
		msg[i] = cb->buf[cb->read_offset];
		cb->read_offset = (cb->read_offset + 1) % CEC_CIRCBUF_SIZE;

	}

	mutex_unlock(&circbuf_readoffset_mutex);

	return 0;
}
