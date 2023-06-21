/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
#define TEST_PORT_1 1

FAKE_VALUE_FUNC(int, mock_init, int);
FAKE_VALUE_FUNC(int, mock_get_enable, int, uint8_t *);
FAKE_VALUE_FUNC(int, mock_set_enable, int, uint8_t);
FAKE_VALUE_FUNC(int, mock_get_logical_addr, int, uint8_t *);
FAKE_VALUE_FUNC(int, mock_set_logical_addr, int, uint8_t);
FAKE_VALUE_FUNC(int, mock_send, int, const uint8_t *, uint8_t);
FAKE_VALUE_FUNC(int, mock_get_received_message, int, uint8_t **, uint8_t *);

struct cec_common_fixture {
	const struct cec_drv *cec_0_drv;
	const struct cec_drv *cec_1_drv;
	struct cec_drv mock_drv;
};

static uint8_t enable_custom_fake;
static int get_enable_custom_fake(int port, uint8_t *enable)
{
	*enable = enable_custom_fake;

	return EC_SUCCESS;
}

static uint8_t logical_addr_custom_fake;
static int get_logical_addr_custom_fake(int port, uint8_t *logical_addr)
{
	*logical_addr = logical_addr_custom_fake;

	return EC_SUCCESS;
}

static uint8_t saved_msg_send_custom_fake[MAX_CEC_MSG_LEN];
static int send_custom_fake(int port, const uint8_t *msg, uint8_t len)
{
	memcpy(saved_msg_send_custom_fake, msg,
	       MIN(len, sizeof(saved_msg_send_custom_fake)));

	return EC_SUCCESS;
}

static void *cec_common_setup(void)
{
	static struct cec_common_fixture fixture = {
		.mock_drv = {
			.init = mock_init,
			.get_enable = mock_get_enable,
			.set_enable = mock_set_enable,
			.get_logical_addr = mock_get_logical_addr,
			.set_logical_addr = mock_set_logical_addr,
			.send = mock_send,
			.get_received_message = mock_get_received_message,
		},
	};

	fixture.cec_0_drv = cec_config[TEST_PORT].drv;
	fixture.cec_1_drv = cec_config[TEST_PORT_1].drv;

	return &fixture;
}

static void cec_common_before(void *fixture)
{
	RESET_FAKE(mock_init);
	RESET_FAKE(mock_get_enable);
	RESET_FAKE(mock_set_enable);
	RESET_FAKE(mock_get_logical_addr);
	RESET_FAKE(mock_set_logical_addr);
	RESET_FAKE(mock_send);
	RESET_FAKE(mock_get_received_message);
	FFF_RESET_HISTORY();
}

static void cec_common_after(void *fixture)
{
	struct cec_common_fixture *f = fixture;

	cec_config[TEST_PORT].drv = f->cec_0_drv;
	cec_config[TEST_PORT_1].drv = f->cec_1_drv;
}

/* Test basic get_bit/set_bit/inc_bit behaviour */
ZTEST_USER(cec_common, test_cec_transfer)
{
	struct cec_msg_transfer transfer = {};
	int i;

	/* Set first byte to 1 */
	for (i = 0; i < 8; i++) {
		cec_transfer_set_bit(&transfer, 1);
		zassert_true(cec_transfer_get_bit(&transfer));
		cec_transfer_inc_bit(&transfer);
		zassert_false(cec_transfer_get_bit(&transfer));
	}
	zassert_equal(transfer.buf[0], 0xff);
	zassert_equal(transfer.buf[1], 0);

	/* Set half of second byte to 1 */
	for (i = 0; i < 4; i++) {
		cec_transfer_set_bit(&transfer, 1);
		zassert_true(cec_transfer_get_bit(&transfer));
		cec_transfer_inc_bit(&transfer);
		zassert_false(cec_transfer_get_bit(&transfer));
	}
	zassert_equal(transfer.buf[0], 0xff);
	zassert_equal(transfer.buf[1], 0xf0);
	zassert_equal(transfer.buf[2], 0);
}

static void test_transfer_is_eom(int len)
{
	struct cec_msg_transfer transfer = {};
	int i;

	/* Write one bit fewer than len bytes and check EOM is false */
	zassert_false(cec_transfer_is_eom(&transfer, len));
	for (i = 0; i < 8 * len - 1; i++) {
		cec_transfer_inc_bit(&transfer);
		zassert_false(cec_transfer_is_eom(&transfer, len));
	}

	/* Write one more bit and check EOM is true */
	cec_transfer_inc_bit(&transfer);
	zassert_true(cec_transfer_is_eom(&transfer, len));
}

ZTEST_USER(cec_common, test_cec_transfer_is_eom)
{
	test_transfer_is_eom(1);
	test_transfer_is_eom(2);
	test_transfer_is_eom(3);
	test_transfer_is_eom(MAX_CEC_MSG_LEN);
}

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

	/* Check that cec_transfer_get_bit does not read past the transfer */
	overflow_msg.overflow_detector = 0xff;
	overflow_msg.transfer.bit = 0;
	zassert_equal(cec_transfer_get_bit(&overflow_msg.transfer), 0);
}

static bool msg_is_equal(const uint8_t *msg1, const uint8_t msg1_len,
			 const uint8_t *msg2, const uint8_t msg2_len)
{
	int i;

	if (msg1_len != msg2_len)
		return false;

	for (i = 0; i < msg1_len; i++)
		if (msg1[i] != msg2[i])
			return false;

	return true;
}

/* Test basic push/pop/flush behaviour */
ZTEST_USER(cec_common, test_cec_rx_queue)
{
	struct cec_rx_queue queue = {};
	const uint8_t msg1[] = { 0x0f, 0x87, 0x00, 0xe0, 0x91 };
	const uint8_t msg1_len = ARRAY_SIZE(msg1);
	const uint8_t msg2[] = { 0x04, 0x46 };
	const uint8_t msg2_len = ARRAY_SIZE(msg2);
	uint8_t msg[MAX_CEC_MSG_LEN];
	uint8_t msg_len;
	int i;

	/* Queue is empty so pop returns an error */
	zassert_not_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);

	/* Push two messages */
	zassert_equal(cec_rx_queue_push(&queue, msg1, msg1_len), EC_SUCCESS);
	zassert_equal(cec_rx_queue_push(&queue, msg2, msg2_len), EC_SUCCESS);

	/* Pop the messages and check they're correct */
	zassert_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);
	zassert_true(msg_is_equal(msg, msg_len, msg1, msg1_len));
	zassert_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);
	zassert_true(msg_is_equal(msg, msg_len, msg2, msg2_len));

	/* Check queue is empty */
	zassert_not_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);

	/* Push and pop multiple times to check offsets wrap around correctly */
	for (i = 0; i < (CEC_RX_BUFFER_SIZE * 2) / msg1_len; i++) {
		zassert_equal(cec_rx_queue_push(&queue, msg1, msg1_len),
			      EC_SUCCESS);
		zassert_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);
		zassert_true(msg_is_equal(msg, msg_len, msg1, msg1_len));
	}

	/* Check queue is empty */
	zassert_not_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);

	/* Check flush works */
	zassert_equal(cec_rx_queue_push(&queue, msg1, msg1_len), EC_SUCCESS);
	zassert_equal(cec_rx_queue_push(&queue, msg2, msg2_len), EC_SUCCESS);
	cec_rx_queue_flush(&queue);
	zassert_not_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);

	/* Push a message then corrupt the message length in the queue */
	zassert_equal(cec_rx_queue_push(&queue, msg1, msg1_len), EC_SUCCESS);
	queue.buf[queue.read_offset] = MAX_CEC_MSG_LEN + 1;
	/* Check pop returns an error */
	zassert_not_equal(cec_rx_queue_pop(&queue, msg, &msg_len), 0);
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

static int host_cmd_cec_set(int port, enum cec_command cmd, uint8_t val)
{
	struct ec_params_cec_set params = {
		.cmd = cmd,
		.port = port,
		.val = val,
	};

	return ec_cmd_cec_set(NULL, &params);
}

ZTEST_USER_F(cec_common, test_hc_cec_set_invalid_param)
{
	/* Invalid port */
	zassert_equal(host_cmd_cec_set(CEC_PORT_COUNT, CEC_CMD_ENABLE, 0),
		      EC_RES_INVALID_PARAM);

	/* Invalid cmd */
	zassert_equal(host_cmd_cec_set(TEST_PORT, 7, 0), EC_RES_INVALID_PARAM);

	/* Invalid enable val */
	zassert_equal(host_cmd_cec_set(TEST_PORT, CEC_CMD_ENABLE, 2),
		      EC_RES_INVALID_PARAM);

	/* Invalid logical_addr val */
	zassert_equal(host_cmd_cec_set(TEST_PORT, CEC_CMD_LOGICAL_ADDRESS,
				       CEC_BROADCAST_ADDR + 1),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER_F(cec_common, test_hc_cec_set_enable_error)
{
	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Driver returns error */
	mock_set_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(host_cmd_cec_set(TEST_PORT, CEC_CMD_ENABLE, 0),
		      EC_RES_ERROR);
}

ZTEST_USER_F(cec_common, test_hc_cec_set_enable_0)
{
	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Set enable to 0 */
	mock_set_enable_fake.return_val = EC_SUCCESS;
	zassert_ok(host_cmd_cec_set(TEST_PORT, CEC_CMD_ENABLE, 0));
	zassert_equal(mock_set_enable_fake.call_count, 1);
	zassert_equal(mock_set_enable_fake.arg0_val, TEST_PORT);
	zassert_equal(mock_set_enable_fake.arg1_val, 0);
}

ZTEST_USER_F(cec_common, test_hc_cec_set_enable_1)
{
	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Set enable to 1 */
	mock_set_enable_fake.return_val = EC_SUCCESS;
	zassert_ok(host_cmd_cec_set(TEST_PORT, CEC_CMD_ENABLE, 1));
	zassert_equal(mock_set_enable_fake.call_count, 1);
	zassert_equal(mock_set_enable_fake.arg0_val, TEST_PORT);
	zassert_equal(mock_set_enable_fake.arg1_val, 1);
}

ZTEST_USER_F(cec_common, test_hc_cec_set_logical_addr_error)
{
	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Driver returns error */
	mock_set_logical_addr_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(host_cmd_cec_set(TEST_PORT, CEC_CMD_LOGICAL_ADDRESS, 0x4),
		      EC_RES_ERROR);
}

ZTEST_USER_F(cec_common, test_hc_cec_set_logical_addr)
{
	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Set logical address to 0x4 */
	mock_set_logical_addr_fake.return_val = EC_SUCCESS;
	zassert_ok(host_cmd_cec_set(TEST_PORT, CEC_CMD_LOGICAL_ADDRESS, 0x4));
	zassert_equal(mock_set_logical_addr_fake.call_count, 1);
	zassert_equal(mock_set_logical_addr_fake.arg0_val, TEST_PORT);
	zassert_equal(mock_set_logical_addr_fake.arg1_val, 0x4);
}

static int host_cmd_cec_get(int port, enum cec_command cmd,
			    struct ec_response_cec_get *response)
{
	struct ec_params_cec_get params = {
		.cmd = cmd,
		.port = port,
	};

	return ec_cmd_cec_get(NULL, &params, response);
}

ZTEST_USER_F(cec_common, test_hc_cec_get_invalid_param)
{
	struct ec_response_cec_get response;

	/* Invalid port */
	zassert_equal(host_cmd_cec_get(CEC_PORT_COUNT, CEC_CMD_ENABLE,
				       &response),
		      EC_RES_INVALID_PARAM);

	/* Invalid cmd */
	zassert_equal(host_cmd_cec_get(TEST_PORT, 7, &response),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER_F(cec_common, test_hc_cec_get_enable_error)
{
	struct ec_response_cec_get response;

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Driver returns error */
	mock_get_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(host_cmd_cec_get(TEST_PORT, CEC_CMD_ENABLE, &response),
		      EC_RES_ERROR);
}

ZTEST_USER_F(cec_common, test_hc_cec_get_enable_0)
{
	struct ec_response_cec_get response;

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Get enable returns 0 */
	enable_custom_fake = 0;
	mock_get_enable_fake.custom_fake = get_enable_custom_fake;
	zassert_ok(host_cmd_cec_get(TEST_PORT, CEC_CMD_ENABLE, &response));
	zassert_equal(mock_get_enable_fake.call_count, 1);
	zassert_equal(mock_get_enable_fake.arg0_val, TEST_PORT);
	zassert_equal(response.val, 0);
}

ZTEST_USER_F(cec_common, test_hc_cec_get_enable_1)
{
	struct ec_response_cec_get response;

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Get enable returns 1 */
	enable_custom_fake = 1;
	mock_get_enable_fake.custom_fake = get_enable_custom_fake;
	zassert_ok(host_cmd_cec_get(TEST_PORT, CEC_CMD_ENABLE, &response));
	zassert_equal(mock_get_enable_fake.call_count, 1);
	zassert_equal(mock_get_enable_fake.arg0_val, TEST_PORT);
	zassert_equal(response.val, 1);
}

ZTEST_USER_F(cec_common, test_hc_cec_get_logical_addr_error)
{
	struct ec_response_cec_get response;

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Driver returns error */
	mock_get_logical_addr_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(host_cmd_cec_get(TEST_PORT, CEC_CMD_LOGICAL_ADDRESS,
				       &response),
		      EC_RES_ERROR);
}

ZTEST_USER_F(cec_common, test_hc_cec_get_logical_addr)
{
	struct ec_response_cec_get response;

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Get logical_addr returns 0x4 */
	logical_addr_custom_fake = 0x4;
	mock_get_logical_addr_fake.custom_fake = get_logical_addr_custom_fake;
	zassert_ok(host_cmd_cec_get(TEST_PORT, CEC_CMD_LOGICAL_ADDRESS,
				    &response));
	zassert_equal(mock_get_logical_addr_fake.call_count, 1);
	zassert_equal(mock_get_logical_addr_fake.arg0_val, TEST_PORT);
	zassert_equal(response.val, 0x4);
}

static int host_cmd_cec_write(const uint8_t *msg, uint8_t msg_len)
{
	struct ec_params_cec_write params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CEC_WRITE_MSG, 0, params);

	memcpy(params.msg, msg, MIN(msg_len, sizeof(params.msg)));
	args.params_size = msg_len;

	return host_command_process(&args);
}

static int host_cmd_cec_write_v1(int port, const uint8_t *msg, uint8_t msg_len)
{
	struct ec_params_cec_write_v1 params_v1;

	params_v1.port = port;
	params_v1.msg_len = msg_len;
	memcpy(params_v1.msg, msg, MIN(msg_len, sizeof(params_v1.msg)));

	return ec_cmd_cec_write_v1(NULL, &params_v1);
}

ZTEST_USER_F(cec_common, test_hc_cec_write_v0_invalid_param)
{
	const uint8_t msg[] = { 0x4f, 0x87, 0x00, 0x0c, 0x03 };

	/* Invalid msg_len */
	zassert_equal(host_cmd_cec_write(msg, 0), EC_RES_INVALID_PARAM);
	zassert_equal(host_cmd_cec_write(msg, MAX_CEC_MSG_LEN + 1),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER_F(cec_common, test_hc_cec_write_v1_invalid_param)
{
	const uint8_t msg[] = { 0x4f, 0x87, 0x00, 0x0c, 0x03 };
	const uint8_t msg_len = ARRAY_SIZE(msg);

	/* Invalid port */
	zassert_equal(host_cmd_cec_write_v1(CEC_PORT_COUNT, msg, msg_len),
		      EC_RES_INVALID_PARAM);

	/* Invalid msg_len */
	zassert_equal(host_cmd_cec_write_v1(TEST_PORT, msg, 0),
		      EC_RES_INVALID_PARAM);
	zassert_equal(host_cmd_cec_write_v1(TEST_PORT, msg,
					    MAX_CEC_MSG_LEN + 1),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER_F(cec_common, test_hc_cec_write_v0_error)
{
	const uint8_t msg[] = { 0x4f, 0x87, 0x00, 0x0c, 0x03 };
	const uint8_t msg_len = ARRAY_SIZE(msg);

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Driver returns error */
	mock_send_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(host_cmd_cec_write(msg, msg_len), EC_RES_BUSY);
}

ZTEST_USER_F(cec_common, test_hc_cec_write_v1_error)
{
	const uint8_t msg[] = { 0x4f, 0x87, 0x00, 0x0c, 0x03 };
	const uint8_t msg_len = ARRAY_SIZE(msg);

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Driver returns error */
	mock_send_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(host_cmd_cec_write_v1(TEST_PORT, msg, msg_len),
		      EC_RES_BUSY);
}

ZTEST_USER_F(cec_common, test_hc_cec_write_v0)
{
	const uint8_t msg[] = { 0x4f, 0x87, 0x00, 0x0c, 0x03 };
	const uint8_t msg_len = ARRAY_SIZE(msg);

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Write succeeds */
	mock_send_fake.custom_fake = send_custom_fake;
	zassert_ok(host_cmd_cec_write(msg, msg_len));
	zassert_equal(mock_send_fake.call_count, 1);
	zassert_equal(mock_send_fake.arg0_val, TEST_PORT);
	zassert_ok(memcmp(saved_msg_send_custom_fake, msg, msg_len));
	zassert_equal(mock_send_fake.arg2_val, msg_len);
}

ZTEST_USER_F(cec_common, test_hc_cec_write_v1)
{
	const uint8_t msg[] = { 0x4f, 0x87, 0x00, 0x0c, 0x03 };
	const uint8_t msg_len = ARRAY_SIZE(msg);

	cec_config[TEST_PORT].drv = &fixture->mock_drv;

	/* Write succeeds */
	mock_send_fake.custom_fake = send_custom_fake;
	zassert_ok(host_cmd_cec_write_v1(TEST_PORT, msg, msg_len));
	zassert_equal(mock_send_fake.call_count, 1);
	zassert_equal(mock_send_fake.arg0_val, TEST_PORT);
	zassert_ok(memcmp(saved_msg_send_custom_fake, msg, msg_len));
	zassert_equal(mock_send_fake.arg2_val, msg_len);
}

static int
host_cmd_get_next_event_v2(struct ec_response_get_next_event_v1 *response)
{
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_NEXT_EVENT, 2, *response);

	return host_command_process(&args);
}

static int get_next_cec_mkbp_event(struct ec_response_get_next_event_v1 *event)
{
	/* Read MKBP events until we find one of type CEC_EVENT */
	while (host_cmd_get_next_event_v2(event) == EC_RES_SUCCESS) {
		if ((event->event_type & EC_MKBP_EVENT_TYPE_MASK) ==
		    EC_MKBP_EVENT_CEC_EVENT)
			return 0;
	}
	/* No more events */
	return -1;
}

static bool cec_event_matches(struct ec_response_get_next_event_v1 *event,
			      int port, enum mkbp_cec_event events)
{
	return ((EC_MKBP_EVENT_CEC_GET_PORT(event->data.cec_events) == port) &&
		(EC_MKBP_EVENT_CEC_GET_EVENTS(event->data.cec_events) ==
		 events));
}

ZTEST_USER_F(cec_common, test_mkbp_event_send_ok)
{
	struct ec_response_get_next_event_v1 event;

	/* Set task event and wait 1s to allow task to run */
	cec_task_set_event(TEST_PORT, CEC_TASK_EVENT_OKAY);
	k_sleep(K_SECONDS(1));

	/* Check MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));

	/* Check there are no more events */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER_F(cec_common, test_mkbp_event_send_failed)
{
	struct ec_response_get_next_event_v1 event;

	/* Set task event and wait 1s to allow task to run */
	cec_task_set_event(TEST_PORT, CEC_TASK_EVENT_FAILED);
	k_sleep(K_SECONDS(1));

	/* Check MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_FAILED));

	/* Check there are no more events */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER_F(cec_common, test_mkbp_event_multiple_send_results)
{
	struct ec_response_get_next_event_v1 event;

	/* Set two send results on the same port */
	cec_task_set_event(TEST_PORT, CEC_TASK_EVENT_OKAY);
	k_sleep(K_SECONDS(1));
	cec_task_set_event(TEST_PORT, CEC_TASK_EVENT_FAILED);
	k_sleep(K_SECONDS(1));

	/* Only the most recent send result is kept */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_FAILED));

	/* Check there are no more events */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER_F(cec_common, test_mkbp_event_no_events)
{
	struct ec_response_get_next_event_v1 event;

	/* Send a MKBP event without setting any events */
	mkbp_send_event(EC_MKBP_EVENT_CEC_EVENT);

	/* Check an event is available, but the data is zero */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, 0, 0));

	/* Check there are no more events */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER_F(cec_common, test_mkbp_event_multiple_ports)
{
	struct ec_response_get_next_event_v1 event;

	/* Set events on two different ports */
	cec_task_set_event(TEST_PORT_1, CEC_TASK_EVENT_FAILED);
	k_sleep(K_SECONDS(1));
	cec_task_set_event(TEST_PORT, CEC_TASK_EVENT_OKAY);
	k_sleep(K_SECONDS(1));

	/* Check we can retrieve all events */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT_1,
				       EC_MKBP_CEC_SEND_FAILED));

	/* Check there are no more events */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_SUITE(cec_common, drivers_predicate_post_main, cec_common_setup,
	    cec_common_before, cec_common_after, NULL);
