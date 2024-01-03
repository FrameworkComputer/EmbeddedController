/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_one_wire_uart

#include "console.h"
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);

struct one_wire_uart_config {
	const struct device *bus;
	int id;
};

static int msg_len(const struct one_wire_uart_message *msg)
{
	return sizeof(msg->header) + msg->header.payload_len;
}

test_export_static uint16_t checksum(const struct one_wire_uart_message *msg)
{
	uint32_t sum = 0;
	const uint8_t *data = (const uint8_t *)msg;
	int len = msg_len(msg);

	/* 16-bit one's complement sum */
	for (int i = 0; i < len; i += 2) {
		if (i + 1 < len) {
			sum += sys_le16_to_cpu(*(const uint16_t *)(data + i));
		} else {
			/* last byte */
			sum += data[i];
		}
	}
	/* carry */
	while (sum > 0xFFFF) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return sum;
}

static bool verify_checksum(struct one_wire_uart_message *msg)
{
	uint16_t expected = msg->header.checksum;

	msg->header.checksum = 0;

	return checksum(msg) == expected;
}

int one_wire_uart_send(const struct device *dev, uint8_t cmd,
		       const uint8_t *payload, int size)
{
	struct one_wire_uart_message msg;
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;
	struct one_wire_uart_data *data = dev->data;
	struct k_msgq *tx_queue = data->tx_queue;
	int ret;

	if (size > ONE_WIRE_UART_MAX_PAYLOAD_SIZE) {
		return -EINVAL;
	}

	msg.header = (struct one_wire_uart_header){
		.magic = HEADER_MAGIC,
		.payload_len = size + 1,
		.sender = config->id,
		.msg_id = data->msg_id++ % 32,
		.ack = 0,
		.reset = 0,
		.checksum = 0,
	};
	msg.payload[0] = cmd;

	memcpy(msg.payload + 1, payload, size);
	msg.header.checksum = checksum(&msg);

	ret = k_msgq_put(tx_queue, &msg, K_NO_WAIT);

	if (!ret) {
		uart_irq_tx_enable(bus);
	}
	return ret;
}

static int one_wire_uart_send_reset(const struct device *dev)
{
	struct one_wire_uart_message msg;
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;
	struct one_wire_uart_data *data = dev->data;
	struct k_msgq *tx_queue = data->tx_queue;
	int ret;

	msg.header = (struct one_wire_uart_header){
		.magic = HEADER_MAGIC,
		.payload_len = 0,
		.sender = config->id,
		.msg_id = 0,
		.ack = 0,
		.reset = 1,
		.checksum = 0,
	};
	msg.header.checksum = checksum(&msg);

	ret = k_msgq_put(tx_queue, &msg, K_NO_WAIT);

	if (!ret) {
		uart_irq_tx_enable(bus);
	}
	return ret;
}

/* consume rx queue in non-irq context */
test_export_static void process_packet(void)
{
	struct one_wire_uart_message msg;
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	struct one_wire_uart_data *data = dev->data;
	struct k_msgq *rx_queue = data->rx_queue;

	while (k_msgq_get(rx_queue, &msg, K_NO_WAIT) == 0) {
		int last_msg_id = data->last_received_msg_id;

		if (last_msg_id != msg.header.msg_id && data->msg_received_cb) {
			data->msg_received_cb(msg.payload[0], msg.payload + 1,
					      msg.header.payload_len - 1);
		}
		data->last_received_msg_id = msg.header.msg_id;
	}
}
DECLARE_DEFERRED(process_packet);

static void gen_ack_response(const struct device *dev,
			     struct one_wire_uart_message *msg, int msg_id)
{
	const struct one_wire_uart_config *config = dev->config;

	msg->header = (struct one_wire_uart_header){
		.magic = HEADER_MAGIC,
		.payload_len = 0,
		.sender = config->id,
		.msg_id = msg_id,
		.ack = 1,
		.reset = 0,
		.checksum = 0,
	};

	msg->header.checksum = checksum(msg);
}

static void wake_tx(void)
{
	uart_irq_tx_enable(DEVICE_DT_GET(DT_INST_PARENT(0)));
}
DECLARE_DEFERRED(wake_tx);

/* retry every 2.5ms */
#define RETRY_INTERVAL (5 * MSEC / 2)
#define MAX_RETRY 10

static void start_error_recovery(void)
{
	ccprints("one_wire_uart: reached max retry count, trying reset");
	one_wire_uart_send_reset(DEVICE_DT_GET(DT_DRV_INST(0)));
}
DECLARE_DEFERRED(start_error_recovery);

test_export_static void load_next_message(const struct device *dev)
{
	struct one_wire_uart_data *data = dev->data;
	struct ring_buf *tx_ring_buf = data->tx_ring_buf;
	struct k_msgq *tx_queue = data->tx_queue;
	struct one_wire_uart_message *msg = &data->resend_cache;

	if (!ring_buf_is_empty(tx_ring_buf)) {
		data->last_send_time = get_time();

		return;
	}

	if (data->msg_pending && data->ack == msg->header.msg_id) {
		data->ack = -1;
		data->msg_pending = false;
	}

	if (!data->msg_pending) {
		data->msg_pending = (k_msgq_get(tx_queue, msg, K_NO_WAIT) == 0);
		data->retry_count = 0;
	}

	if (data->msg_pending) {
		unsigned int elapsed = time_since32(data->last_send_time);
		bool can_send = data->retry_count == 0 ||
				elapsed >= RETRY_INTERVAL;

		if (can_send && data->retry_count >= MAX_RETRY) {
			one_wire_uart_reset(dev);

			/* if the failed message is not a RESET message, try to
			 * reset remote first. Otherwise, silently stop ourself.
			 */
			if (!msg->header.reset) {
				hook_call_deferred(&start_error_recovery_data,
						   0);
			}
		} else if (can_send) {
			int len = msg_len(msg);

			ring_buf_put(tx_ring_buf, (uint8_t *)msg, len);
			data->last_send_time = get_time();
			++data->retry_count;
		} else {
			hook_call_deferred(&wake_tx_data,
					   RETRY_INTERVAL - elapsed);
		}
	}
}

test_export_static void process_tx_irq(const struct device *dev)
{
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;
	int filled = 0;
	struct one_wire_uart_data *data = dev->data;
	struct ring_buf *tx_ring_buf = data->tx_ring_buf;

	load_next_message(dev);

	if (!ring_buf_is_empty(tx_ring_buf)) {
		uint8_t *data;
		int data_size = ring_buf_get_claim(tx_ring_buf, &data, 16);

		filled = uart_fifo_fill(bus, data, data_size);
		ring_buf_get_finish(tx_ring_buf, MAX(filled, 0));
	}

	if (filled <= 0 && uart_irq_tx_complete(bus)) {
		uart_irq_tx_disable(bus);
	}
}

/* find the first occurrence of the header magic in the ring buffer */
test_export_static void find_header(const struct device *dev)
{
	struct one_wire_uart_data *data = dev->data;
	struct ring_buf *rx_ring_buf = data->rx_ring_buf;

	while (!ring_buf_is_empty(rx_ring_buf)) {
		uint8_t *data;
		uint32_t claimed = ring_buf_get_claim(rx_ring_buf, &data, 512);

		uint8_t *ptr = memchr(data, HEADER_MAGIC, claimed);

		if (!ptr) {
			ring_buf_get_finish(rx_ring_buf, claimed);
			continue;
		}

		ring_buf_get_finish(rx_ring_buf, ptr - data);
		break;
	}
}

test_export_static void process_rx_fifo(const struct device *dev)
{
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;
	struct one_wire_uart_data *data = dev->data;
	struct ring_buf *tx_ring_buf = data->tx_ring_buf;
	struct ring_buf *rx_ring_buf = data->rx_ring_buf;
	struct k_msgq *rx_queue = data->rx_queue;
	struct one_wire_uart_message msg;

	while (true) {
		int len;

		find_header(dev);

		/* If the size isn't at least the header size large,
		 * then it's an incomplete packet. Wait for next RX irq.
		 */
		if (ring_buf_size_get(rx_ring_buf) <
		    sizeof(struct one_wire_uart_header)) {
			break;
		}
		ring_buf_peek(rx_ring_buf, (uint8_t *)&msg.header,
			      sizeof(struct one_wire_uart_header));

		/* bad length */
		if (msg.header.payload_len > ONE_WIRE_UART_MAX_PAYLOAD_SIZE) {
			ring_buf_get(rx_ring_buf, NULL, 1);
			continue;
		}

		/* If the size isn't match the msg_len, then it's an
		 * incomplete packet.
		 */
		len = msg_len(&msg);
		if (ring_buf_size_get(rx_ring_buf) < len) {
			break;
		}

		ring_buf_peek(rx_ring_buf, (uint8_t *)&msg, len);

		/* bad checksum, drop 1 byte and loop again */
		if (!verify_checksum(&msg)) {
			ring_buf_get(rx_ring_buf, NULL, 1);
			continue;
		}

		/* proceed if the message is not sent by ourselves */
		if (msg.header.sender != config->id) {
			int msg_id = msg.header.msg_id;

			if (msg.header.ack) {
				data->ack = msg_id;
			} else {
				struct one_wire_uart_message ack_resp;

				if (msg.header.reset) {
					one_wire_uart_reset(dev);
				} else {
					k_msgq_put(rx_queue, &msg, K_NO_WAIT);
					hook_call_deferred(&process_packet_data,
							   0);
				}

				gen_ack_response(dev, &ack_resp, msg_id);
				ring_buf_put(tx_ring_buf, (uint8_t *)&ack_resp,
					     sizeof(ack_resp.header));
				uart_irq_tx_enable(bus);
			}
		}

		/* drop `len` bytes from `rx_ring_buf` */
		ring_buf_get(rx_ring_buf, NULL, len);
	}
}

void load_rx_fifo(const struct device *dev)
{
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;
	struct one_wire_uart_data *data = dev->data;
	struct ring_buf *rx_ring_buf = data->rx_ring_buf;

	while (true) {
		uint8_t buf[16];

		int ret = uart_fifo_read(bus, buf, sizeof(buf));

		if (ret > 0) {
			/* This function may return less allocated buffer size
			 * than requested (e.g. when ring buf if full), but
			 * the chance is very low so we don't check the return
			 * size.
			 */
			ring_buf_put(rx_ring_buf, buf, ret);
		}

		if (ret < sizeof(buf)) {
			break;
		}
	}
}

void uart_handler(const struct device *bus, void *user_data)
{
	const struct device *dev = user_data;

	uart_irq_update(bus);

	if (uart_irq_rx_ready(bus)) {
		load_rx_fifo(dev);
		process_rx_fifo(dev);
	}

	if (uart_irq_tx_ready(bus)) {
		process_tx_irq(dev);
	}
}

/* reset internal state */
void one_wire_uart_reset(const struct device *dev)
{
	struct one_wire_uart_data *data = dev->data;

	/* reset internal states */
	data->last_received_msg_id = -1;
	data->ack = -1;
	data->msg_pending = false;
	data->msg_id = 0;

	k_msgq_purge(data->tx_queue);
	k_msgq_purge(data->rx_queue);
	ring_buf_reset(data->tx_ring_buf);
	ring_buf_reset(data->rx_ring_buf);
}

void one_wire_uart_enable(const struct device *dev)
{
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;

	one_wire_uart_reset(dev);
	uart_irq_callback_user_data_set(bus, uart_handler, (void *)dev);
	uart_irq_rx_enable(bus);
	one_wire_uart_send_reset(dev);
}

void one_wire_uart_set_callback(const struct device *dev,
				one_wire_uart_msg_received_cb_t msg_received)
{
	struct one_wire_uart_data *data = dev->data;

	data->msg_received_cb = msg_received;
}

#define SELF_ID(n) \
	DT_INST_PROP_OR(n, id, !!IS_ENABLED(CONFIG_PLATFORM_EC_DETACHABLE_BASE))

#define INIT_ONE_WIRE_UART_DEVICE(n)                                         \
	static const struct one_wire_uart_config one_wire_uart_config##n = { \
		.bus = DEVICE_DT_GET(DT_INST_PARENT(n)),                     \
		.id = SELF_ID(n),                                            \
	};                                                                   \
	RING_BUF_DECLARE(tx_ring_buf##n, 128);                               \
	RING_BUF_DECLARE(rx_ring_buf##n, 128);                               \
	K_MSGQ_DEFINE(rx_queue##n, sizeof(struct one_wire_uart_message), 32, \
		      1);                                                    \
	K_MSGQ_DEFINE(tx_queue##n, sizeof(struct one_wire_uart_message), 32, \
		      1);                                                    \
	static struct one_wire_uart_data one_wire_uart_data##n = {           \
		.msg_id = 0,                                                 \
		.last_received_msg_id = -1,                                  \
		.tx_ring_buf = &tx_ring_buf##n,                              \
		.rx_ring_buf = &rx_ring_buf##n,                              \
		.tx_queue = &tx_queue##n,                                    \
		.rx_queue = &rx_queue##n,                                    \
		.ack = -1,                                                   \
	};                                                                   \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &one_wire_uart_data##n,         \
			      &one_wire_uart_config##n, POST_KERNEL, 50, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_ONE_WIRE_UART_DEVICE);
