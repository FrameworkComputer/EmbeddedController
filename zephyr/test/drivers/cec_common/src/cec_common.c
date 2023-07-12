/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

struct overflow_msg {
	struct cec_msg_transfer transfer;
	uint8_t overflow_detector;
};
/* Ensure the overflow detector is located directly after the buffer */
BUILD_ASSERT(offsetof(struct overflow_msg, overflow_detector) ==
	     offsetof(struct cec_msg_transfer, buf) + MAX_CEC_MSG_LEN);

ZTEST_USER(cec_common, test_cec_transfer_overflow)
{
	struct overflow_msg overflow_msg = {};
	int i;

	/* Overwrite the buffer by 1 byte */
	for (i = 0; i < (MAX_CEC_MSG_LEN + 1) * 8; i++) {
		cec_transfer_set_bit(&overflow_msg.transfer, 1);
		cec_transfer_inc_bit(&overflow_msg.transfer);
	}

	/* Make sure we actually wrote the whole buffer with ones */
	for (i = 0; i < MAX_CEC_MSG_LEN; i++)
		zassert_equal(overflow_msg.transfer.buf[i], 0xff);

	/* Verify that the attempt to overflow the buffer did not succeed */
	zassert_equal(overflow_msg.overflow_detector, 0);

	/* The full indicator is when byte reaches MAX_CEC_MSG_LEN */
	zassert_equal(overflow_msg.transfer.byte, MAX_CEC_MSG_LEN);

	/* Check that the indicator stays the same if we write another byte */
	for (i = 0; i < 8; i++) {
		cec_transfer_set_bit(&overflow_msg.transfer, 1);
		cec_transfer_inc_bit(&overflow_msg.transfer);
	}
	zassert_equal(overflow_msg.transfer.byte, MAX_CEC_MSG_LEN);
}

struct overflow_queue {
	struct cec_rx_queue queue;
	uint8_t overflow_detector[CEC_RX_BUFFER_SIZE];
};
/* Ensure the overflow detector is located directly after the buffer */
BUILD_ASSERT(offsetof(struct overflow_queue, overflow_detector) ==
	     offsetof(struct cec_rx_queue, buf) + CEC_RX_BUFFER_SIZE);

static int verify_no_queue_overflow(const struct overflow_queue *overflow_queue)
{
	int i;

	for (i = 0; i < CEC_RX_BUFFER_SIZE; i++) {
		if (overflow_queue->overflow_detector[i] != 0)
			return EC_ERROR_OVERFLOW;
	}
	return EC_SUCCESS;
}

static void clear_queue(struct cec_rx_queue *queue)
{
	memset(queue, 0, sizeof(struct cec_rx_queue));
}

static int fill_queue(struct overflow_queue *overflow_queue, uint8_t *msg,
		      int msg_size)
{
	int i;
	struct cec_rx_queue *queue = &overflow_queue->queue;

	/*
	 * Fill the queue. Every push adds the message and one extra byte for
	 * the length field. The maximum data we can add is one less than
	 * CEC_RX_BUFFER_SIZE since write_pointer==read_pointer is used to
	 * indicate an empty buffer
	 */
	clear_queue(queue);

	for (i = 0; i < (CEC_RX_BUFFER_SIZE - 1) / (msg_size + 1); i++)
		zassert_equal(cec_rx_queue_push(queue, msg, msg_size),
			      EC_SUCCESS);

	/* Now the queue should be full */
	zassert_equal(cec_rx_queue_push(queue, msg, msg_size),
		      EC_ERROR_OVERFLOW);

	/* Verify nothing was written outside of the queue */
	zassert_equal(verify_no_queue_overflow(overflow_queue), EC_SUCCESS);

	return EC_SUCCESS;
}

ZTEST_USER(cec_common, test_cec_rx_queue_overflow)
{
	struct overflow_queue overflow_queue = {};
	uint8_t msg[CEC_RX_BUFFER_SIZE];

	memset(msg, 0xff, sizeof(msg));

	zassert_equal(fill_queue(&overflow_queue, msg, 1), EC_SUCCESS);
	zassert_equal(fill_queue(&overflow_queue, msg, 2), EC_SUCCESS);
	zassert_equal(fill_queue(&overflow_queue, msg, 3), EC_SUCCESS);
	zassert_equal(fill_queue(&overflow_queue, msg, MAX_CEC_MSG_LEN),
		      EC_SUCCESS);
}

ZTEST_SUITE(cec_common, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
