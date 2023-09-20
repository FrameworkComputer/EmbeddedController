/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ONE_WIRE_UART_H_
#define ZEPHYR_INCLUDE_DRIVERS_ONE_WIRE_UART_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#define ONE_WIRE_UART_MAX_PAYLOAD_SIZE 62

/**
 * @brief Send a message.
 *
 * Send the message (cmd, payload) through one-wire UART device.
 * payload must not exceed ONE_WIRE_UART_MAX_PAYLOAD_SIZE(=254) bytes.
 *
 * This is a non-blocking call. may fail if internal buffer is full.
 *
 * @param dev One-Wire UART device instance.
 * @cmd Application defined command to send.
 * @payload Application defined data to send.
 * @size Size of the payload, does not include the cmd byte.
 *
 * @retval 0 if successful
 * @retval -errno Negative errno code in case of failure.
 */
int one_wire_uart_send(const struct device *dev, uint8_t cmd,
		       const uint8_t *payload, int size);

/**
 * @brief Enable the one-wire UART device.
 *
 * Starts Tx and Rx for this device.
 *
 * @param dev One-Wire UART device instance.
 */
void one_wire_uart_enable(const struct device *dev);

/**
 * @brief Application callback function signature for
 * one_wire_uart_set_callback().
 *
 * @param cmd Received command.
 * @param payload Received payload.
 * @param size Size of payload.
 */
typedef void (*one_wire_uart_msg_received_cb_t)(uint8_t cmd,
						const uint8_t *payload,
						int size);

/**
 * @brief Set the on receive callback.
 *
 * This sets up the callback function for message received event.
 * When a message received, the specified function will be called with
 * the cmd and payload data received.
 *
 * @param dev One-Wire UART device instance.
 * @msg_received Pointer to the callback function
 */
void one_wire_uart_set_callback(const struct device *device,
				one_wire_uart_msg_received_cb_t msg_received);

#endif /* ZEPHYR_INCLUDE_DRIVERS_ONE_WIRE_UART_H_ */
