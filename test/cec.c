/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test the buffer handling of HDMI CEC
 */

#include <string.h>

#include "cec.h"
#include "test_util.h"

struct overflow_msg {
	struct cec_msg_transfer transfer;
	uint8_t overflow_detector;
} overflow_msg;
/* Ensure the overflow detector is located directly after the buffer */
BUILD_ASSERT(offsetof(struct overflow_msg, overflow_detector) ==
	     offsetof(struct cec_msg_transfer, buf) + MAX_CEC_MSG_LEN);


struct overflow_queue {
	struct cec_rx_queue queue;
	uint8_t overflow_detector[CEC_RX_BUFFER_SIZE];
} overflow_queue;
/* Ensure the overflow detector is located directly after the buffer */
BUILD_ASSERT(offsetof(struct overflow_queue, overflow_detector) ==
	     offsetof(struct cec_rx_queue, buf) + CEC_RX_BUFFER_SIZE);

static struct cec_rx_queue *queue;

/* Tests */
static int test_msg_overflow(void)
{
	int i;

	/* Overwrite the buffer by 1 byte */
	for (i = 0; i < (MAX_CEC_MSG_LEN+1)*8; i++) {
		cec_transfer_set_bit(&overflow_msg.transfer, 1);
		cec_transfer_inc_bit(&overflow_msg.transfer);
	}

	/* Make sure we actually wrote the whole buffer with ones */
	for (i = 0; i < MAX_CEC_MSG_LEN; i++)
		TEST_ASSERT(overflow_msg.transfer.buf[i] == 0xff);

	/* Verify that the attempt to overflow the buffer did not succeed */
	TEST_ASSERT(overflow_msg.overflow_detector == 0);

	/* The full indicator is when byte reaches MAX_CEC_MSG_LEN */
	TEST_ASSERT(overflow_msg.transfer.byte == MAX_CEC_MSG_LEN);

	/* Check that the indicator stays the same if we write another byte */
	for (i = 0; i < 8; i++) {
		cec_transfer_set_bit(&overflow_msg.transfer, 1);
		cec_transfer_inc_bit(&overflow_msg.transfer);
	}
	TEST_ASSERT(overflow_msg.transfer.byte == MAX_CEC_MSG_LEN);

	return EC_SUCCESS;
}



static int verify_no_queue_overflow(void)
{
	int i;

	for (i = 0; i < CEC_RX_BUFFER_SIZE; i++) {
		if (overflow_queue.overflow_detector[i] != 0)
			return EC_ERROR_OVERFLOW;
	}
	return EC_SUCCESS;
}


static void clear_queue(void)
{
	memset(queue, 0, sizeof(struct cec_rx_queue));

}

static int fill_queue(uint8_t *msg, int msg_size)
{
	int i;

	/*
	 * Fill the queue. Every push adds the message and one extra byte for
	 * the length field. The maximum data we can add is one less than
	 * CEC_RX_BUFFER_SIZE since write_pointer==read_pointer is used to
	 * indicate an empty buffer
	 */
	clear_queue();

	for (i = 0; i < (CEC_RX_BUFFER_SIZE - 1)/(msg_size + 1); i++)
		TEST_ASSERT(cec_rx_queue_push(queue, msg, msg_size) == 0);

	/* Now the queue should be full */
	TEST_ASSERT(cec_rx_queue_push(queue, msg, msg_size) ==
							EC_ERROR_OVERFLOW);

	/* Verify nothing was written outside of the queue */
	TEST_ASSERT(verify_no_queue_overflow() == EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_queue_overflow(void)
{
	uint8_t msg[CEC_RX_BUFFER_SIZE];

	memset(msg, 0xff, sizeof(msg));

	TEST_ASSERT(fill_queue(msg, 1) == EC_SUCCESS);
	TEST_ASSERT(fill_queue(msg, 2) == EC_SUCCESS);
	TEST_ASSERT(fill_queue(msg, 3) == EC_SUCCESS);
	TEST_ASSERT(fill_queue(msg, MAX_CEC_MSG_LEN) == EC_SUCCESS);

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	queue = &overflow_queue.queue;

	RUN_TEST(test_msg_overflow);

	RUN_TEST(test_queue_overflow);

	test_print_result();
}
