/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ONE_WIRE_UART_INTERNAL_H_
#define ZEPHYR_INCLUDE_DRIVERS_ONE_WIRE_UART_INTERNAL_H_

#include "timer.h"

#include <zephyr/kernel.h>

/* Private structures and methods below, for testing purpose only */

struct one_wire_uart_header {
	uint8_t magic;
	uint8_t payload_len;
	uint16_t checksum;
	uint8_t sender : 1;
	uint8_t reset : 1;
	uint8_t ack : 1;
	uint8_t msg_id : 5;
} __packed;

#define HEADER_SIZE sizeof(struct one_wire_uart_header)
BUILD_ASSERT(HEADER_SIZE == 5);

#define HEADER_MAGIC 0xEC

struct one_wire_uart_message {
	struct one_wire_uart_header __aligned(2) header;
	uint8_t payload[ONE_WIRE_UART_MAX_PAYLOAD_SIZE + 1];
} __packed __aligned(2);
BUILD_ASSERT(sizeof(struct one_wire_uart_message) ==
	     HEADER_SIZE + ONE_WIRE_UART_MAX_PAYLOAD_SIZE + 1);

struct one_wire_uart_data {
	one_wire_uart_msg_received_cb_t msg_received_cb;
	int msg_id;
	int last_received_msg_id;

	/* queues for raw bytes */
	struct ring_buf *tx_ring_buf;
	struct ring_buf *rx_ring_buf;

	/* queues for processed messages */
	struct k_msgq *tx_queue;
	struct k_msgq *rx_queue;

	/* id of last ACK message from remote */
	int ack;

	/* resend caches */
	struct one_wire_uart_message resend_cache;
	bool msg_pending;
	timestamp_t last_send_time;
	int retry_count;
};

#ifdef CONFIG_ZTEST
uint16_t checksum(const struct one_wire_uart_message *msg);
void load_next_message(const struct device *dev);
void find_header(const struct device *dev);
void process_rx_fifo(const struct device *dev);
void process_packet(void);
void process_tx_irq(const struct device *dev);
void one_wire_uart_reset(const struct device *dev);
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_ONE_WIRE_UART_INTERNAL_H_ */
