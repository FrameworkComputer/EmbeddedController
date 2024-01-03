/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

const static struct device *dev = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

FAKE_VOID_FUNC(on_message_received, uint8_t, const uint8_t *, int);

ZTEST(one_wire_uart_driver, test_checksum)
{
	struct one_wire_uart_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header.payload_len = 5;
	msg.payload[0] = 0xA1;
	msg.payload[1] = 0xA2;
	msg.payload[2] = 0xA3;
	msg.payload[3] = 0xA4;
	msg.payload[4] = 0xA5;

	/* 16bit words in the msg are [0x0500, 0x0000, 0xA100, 0xA3A2, 0xA5A4]
	 * sum = 0x1EF46 => carry back => 0xEF47
	 */
	zassert_equal(checksum(&msg), 0xEF47);
}

ZTEST(one_wire_uart_driver, test_send)
{
	struct one_wire_uart_data *data = dev->data;
	struct k_msgq *tx_queue = data->tx_queue;

	struct one_wire_uart_message msg;

	one_wire_uart_send(dev, 5, (uint8_t[]){ 6, 7, 8 }, 3);

	k_msgq_get(tx_queue, &msg, K_NO_WAIT);

	zassert_equal(msg.payload[0], 5);
	zassert_equal(msg.payload[1], 6);
	zassert_equal(msg.payload[2], 7);
	zassert_equal(msg.payload[3], 8);
}

ZTEST(one_wire_uart_driver, test_rx)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	uint8_t junk[10] = {};

	memset(&msg, 0, sizeof(msg));

	/* push some junk data first */
	ring_buf_put(data->rx_ring_buf, junk, sizeof(junk));

	/* push 3 messages
	 * expect that on_message_received callback only triggered on the last
	 * message which sender == 1.
	 */
	msg.header.magic = 0xEC;
	msg.header.payload_len = 2;
	msg.payload[0] = 56;
	msg.header.sender = 0;
	msg.header.msg_id = 11;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + 2);

	msg.header.magic = 0xEC;
	msg.header.payload_len = 1;
	msg.payload[0] = 56;
	msg.header.sender = 0;
	msg.header.msg_id = 12;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + 1);

	msg.header.magic = 0xEC;
	msg.header.payload_len = 1;
	msg.payload[0] = 56;
	msg.header.sender = 1;
	msg.header.msg_id = 22;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + 1);

	/* push more junk */
	ring_buf_put(data->rx_ring_buf, junk, sizeof(junk));

	process_rx_fifo(dev);
	process_packet();

	zassert_equal(ring_buf_size_get(data->rx_ring_buf), 0);
	zassert_equal(on_message_received_fake.call_count, 1, "call count %d",
		      on_message_received_fake.call_count);
}

ZTEST(one_wire_uart_driver, test_rx_partial)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = 10;
	msg.payload[0] = 56;
	msg.header.sender = 1;
	msg.header.msg_id = 11;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	/* put 1 byte payload into ring_buf, discard other 9 bytes */
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + 1);

	one_wire_uart_set_callback(dev, on_message_received);

	process_rx_fifo(dev);
	process_packet();

	zassert_equal(on_message_received_fake.call_count, 0);
	zassert_not_equal(ring_buf_size_get(data->rx_ring_buf), 0);
}

ZTEST(one_wire_uart_driver, test_rx_bad_checksum)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = 0;
	msg.header.sender = 1;
	msg.header.msg_id = 11;
	msg.header.checksum = 0;
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg, sizeof(msg.header));

	one_wire_uart_set_callback(dev, on_message_received);

	process_rx_fifo(dev);
	process_packet();

	zassert_equal(on_message_received_fake.call_count, 0);
}

ZTEST(one_wire_uart_driver, test_rx_ack)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = 0;
	msg.header.sender = 1;
	msg.header.ack = 1;
	msg.header.msg_id = 11;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg, sizeof(msg.header));

	process_rx_fifo(dev);

	zassert_equal(ring_buf_size_get(data->rx_ring_buf), 0);
	zassert_equal(data->ack, 11, "ack %d", data->ack);
}

ZTEST(one_wire_uart_driver, test_tx)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	static timestamp_t fake_time;

	get_time_mock = &fake_time;

	/* don't care the actual content, random bytes is fine here */
	msg.header.msg_id = 0;
	msg.header.payload_len = 0;
	k_msgq_put(data->tx_queue, &msg, K_NO_WAIT);

	fake_time.val = 0;
	process_tx_irq(dev);
	/* verify that we enqueued the message to ring_buf */
	zassert_equal(ring_buf_size_get(data->tx_ring_buf), sizeof(msg.header));

	ring_buf_reset(data->tx_ring_buf);
	fake_time.val = MSEC;
	process_tx_irq(dev);
	/* resend timer not expired, shouldn't enqueue any data here */
	zassert_equal(ring_buf_size_get(data->tx_ring_buf), 0);

	ring_buf_reset(data->tx_ring_buf);
	fake_time.val = 3 * MSEC;
	process_tx_irq(dev);
	/* resend timer expired, resend the same message */
	zassert_equal(ring_buf_size_get(data->tx_ring_buf), sizeof(msg.header));

	ring_buf_get(data->tx_ring_buf, NULL, 1);
	fake_time.val = 6 * MSEC;
	process_tx_irq(dev);
	/* resend timer expired, but tx_ring_buf not fully consumed,
	 * don't queue the next pending message.
	 */
	zassert_equal(ring_buf_size_get(data->tx_ring_buf),
		      sizeof(msg.header) - 1);

	ring_buf_reset(data->tx_ring_buf);
	data->ack = 0;
	fake_time.val = 10 * MSEC;
	process_tx_irq(dev);
	/* ACK'ed, nothing queued this time */
	zassert_equal(ring_buf_size_get(data->tx_ring_buf), 0);

	get_time_mock = NULL;
}

ZTEST(one_wire_uart_driver, test_bad_packet_length)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = 255;
	msg.header.sender = 1;
	msg.header.ack = 1;
	msg.header.msg_id = 11;
	msg.header.checksum = 0;
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg, sizeof(msg.header));

	process_rx_fifo(dev);

	/* expect that process_rx_fifo will flush the fifo without waiting for
	 * payload_len(=255) bytes of data arrive
	 */
	zassert_equal(ring_buf_size_get(data->rx_ring_buf), 0);
}

ZTEST(one_wire_uart_driver, test_reset)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.sender = 1;
	msg.header.msg_id = 11;
	msg.header.reset = 1;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg, sizeof(msg.header));

	ring_buf_put(data->tx_ring_buf, "123", 3);

	process_rx_fifo(dev);

	/* expect that
	 * 1. the junk data in tx_ring_buf is cleared
	 * 2. an ack message is push into tx_ring_buf
	 */
	zassert_equal(ring_buf_size_get(data->tx_ring_buf), sizeof(msg.header));
	ring_buf_get(data->tx_ring_buf, (uint8_t *)&msg, sizeof(msg.header));
	zassert_equal(msg.header.ack, 1);
	zassert_equal(msg.header.msg_id, 11);
	zassert_equal(msg.header.reset, 0);
}

ZTEST(one_wire_uart_driver, test_max_retry_count)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	const int MAX_RETRY = 10;
	const k_timeout_t RESEND_DELAY = K_MSEC(3);

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.sender = 1;
	msg.header.msg_id = 11;
	msg.header.checksum = 0;
	msg.header.checksum = checksum(&msg);
	k_msgq_put(data->tx_queue, &msg, K_NO_WAIT);

	for (int i = 0; i < MAX_RETRY; i++) {
		ring_buf_reset(data->tx_ring_buf);
		k_sleep(RESEND_DELAY);
		process_tx_irq(dev);
		zassert_equal(data->retry_count, i + 1);
	}

	/* expect that RESET message is queued */
	ring_buf_reset(data->tx_ring_buf);
	k_sleep(RESEND_DELAY);
	process_tx_irq(dev);
	/* wait for deferred task */
	k_sleep(K_SECONDS(1));
	zassert_ok(k_msgq_peek(data->tx_queue, &msg));
	zassert_equal(msg.header.reset, 1);

	/* send RETRY 10 times */
	for (int i = 0; i < MAX_RETRY; i++) {
		ring_buf_reset(data->tx_ring_buf);
		k_sleep(RESEND_DELAY);
		process_tx_irq(dev);
		zassert_equal(data->retry_count, i + 1);
	}

	/* expect that nothing queued when failed to send RETRY */
	ring_buf_reset(data->tx_ring_buf);
	k_sleep(RESEND_DELAY);
	process_tx_irq(dev);
	zassert_equal(k_msgq_num_used_get(data->tx_queue), 0);
}

struct one_wire_uart_fixture {
	one_wire_uart_msg_received_cb_t orig_cb;
};

static void *one_wire_uart_setup(void)
{
	struct one_wire_uart_data *data = dev->data;

	struct one_wire_uart_fixture *fixture =
		malloc(sizeof(struct one_wire_uart_fixture));

	fixture->orig_cb = data->msg_received_cb;
	one_wire_uart_set_callback(dev, on_message_received);

	return fixture;
}

static void one_wire_uart_driver_before(void *f)
{
	one_wire_uart_reset(dev);

	RESET_FAKE(on_message_received);
}

static void one_wire_uart_teardown(void *f)
{
	struct one_wire_uart_fixture *fixture = f;

	one_wire_uart_set_callback(dev, fixture->orig_cb);
}

ZTEST_SUITE(one_wire_uart_driver, drivers_predicate_post_main,
	    one_wire_uart_setup, one_wire_uart_driver_before, NULL,
	    one_wire_uart_teardown);
