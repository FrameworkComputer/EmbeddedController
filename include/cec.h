/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

/* Size of circular buffer used to store incoming CEC messages */
#define CEC_CIRCBUF_SIZE 20
#if CEC_CIRCBUF_SIZE < MAX_CEC_MSG_LEN + 1
#error "Buffer must fit at least a CEC message and a length byte"
#endif
#if CEC_CIRCBUF_SIZE > 255
#error "Buffer size must not exceed 255 since offsets are uint8_t"
#endif

/* CEC message during transfer */
struct cec_msg_transfer {
	/* Bit offset  */
	uint8_t bit;
	/* Byte offset */
	uint8_t byte;
	/* The CEC message */
	uint8_t buf[MAX_CEC_MSG_LEN];
};

/*
 * Circular buffer of completed incoming CEC messages
 * ready to be read out by AP
 */
struct cec_rx_cb {
	/*
	 * Write offset. Updated from interrupt context when we
	 * have received a complete message.
	 */
	uint8_t write_offset;
	/* Read offset. Updated when AP sends CEC read command */
	uint8_t read_offset;
	/* Cicular buffer data  */
	uint8_t buf[CEC_CIRCBUF_SIZE];
};

int msgt_get_bit(const struct cec_msg_transfer *msgt);

void msgt_set_bit(struct cec_msg_transfer *msgt, int val);

void msgt_inc_bit(struct cec_msg_transfer *msgt);

int msgt_is_eom(const struct cec_msg_transfer *msgt, int len);

void rx_circbuf_flush(struct cec_rx_cb *cb);

int rx_circbuf_push(struct cec_rx_cb *cb, uint8_t *msg, uint8_t msg_len);

int rx_circbuf_pop(struct cec_rx_cb *cb, uint8_t *msg, uint8_t *msg_len);
