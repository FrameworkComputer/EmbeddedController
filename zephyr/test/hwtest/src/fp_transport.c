/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor/fpsensor_detect.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/ztest.h>

static struct ec_host_cmd *hc;
static struct k_sem hc_send;

static int hc_backend_send(const struct ec_host_cmd_backend *backend)
{
	k_sem_give(&hc_send);

	return 0;
}

struct ec_host_cmd_backend_api hc_api = {
	.init = NULL,
	.send = hc_backend_send,
};

/* Define the test backend. */
struct ec_host_cmd_backend hc_backend = {
	.api = &hc_api,
	.ctx = NULL,
};

static const struct ec_response_get_protocol_info expected_info[] = {
	[FP_TRANSPORT_TYPE_SPI] = {
		.flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED,
		.max_response_packet_size = 544,
		.max_request_packet_size = 544,
		.protocol_versions = BIT(3),
	},
	[FP_TRANSPORT_TYPE_UART] = {
		.flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED,
		.max_response_packet_size = 256,
		.max_request_packet_size = 544,
		.protocol_versions = BIT(3),
	}
};

static void *fpsensor_setup_spi(void)
{
	hc = (struct ec_host_cmd *)ec_host_cmd_get_hc();

	k_sem_init(&hc_send, 0, 1);

	/* Use the test HC backend. */
	hc->backend = &hc_backend;

	return NULL;
}

static uint8_t cal_checksum(const uint8_t *const buffer, const uint16_t size)
{
	uint8_t checksum = 0;

	for (size_t i = 0; i < size; ++i) {
		checksum += buffer[i];
	}
	return (uint8_t)(-checksum);
}

ZTEST_SUITE(fp_transport_spi, NULL, fpsensor_setup_spi, NULL, NULL, NULL);

/* SPI is a default HC backend. Issue a EC_CMD_GET_PROTOCOL_INFO command via
 * test backend to get protocol_info for SPI.
 */
ZTEST(fp_transport_spi, test_fp_transport_spi)
{
	const struct ec_response_get_protocol_info *expected =
		&expected_info[FP_TRANSPORT_TYPE_SPI];
	struct ec_response_get_protocol_info *info;
	struct ec_host_cmd_request_header header = {
		.prtcl_ver = 3,
		.checksum = 0,
		.cmd_id = EC_CMD_GET_PROTOCOL_INFO,
		.cmd_ver = 0,
		.reserved = 0,
		.data_len = 0,
	};
	struct ec_host_cmd_response_header *response;

	/* Prepare header to send. */
	header.checksum = cal_checksum((uint8_t *)&header, sizeof(header));
	memcpy(hc->rx_ctx.buf, &header, sizeof(header));

	/* Notify HC subsystem about a new command. */
	hc->rx_ctx.len = sizeof(header);
	ec_host_cmd_rx_notify();

	/* Confirm a response has been sent via the test backend. */
	k_sem_take(&hc_send, K_FOREVER);
	response = hc->tx.buf;
	info = (void *)((uint8_t *)hc->tx.buf + sizeof(header));

	zassert_equal(response->result, EC_HOST_CMD_SUCCESS);
	zassert_equal(info->flags, expected->flags);
	zassert_equal(info->max_request_packet_size,
		      expected->max_request_packet_size);
	zassert_equal(info->max_response_packet_size,
		      expected->max_response_packet_size);
	zassert_equal(info->protocol_versions, expected->protocol_versions);
}

ZTEST_SUITE(fp_transport_uart, NULL, NULL, NULL, NULL, NULL);

/* UART uses the same EC_CMD_GET_PROTOCOL_INFO HC handler as SPI. If the
 * output from the command if correct for SPI, it should be correct for UART as
 * well. It would be hard to replace already initialized HC backed (SPI).
 */
ZTEST(fp_transport_uart, test_fp_transport_uart)
{
	const struct ec_response_get_protocol_info *expected =
		&expected_info[FP_TRANSPORT_TYPE_UART];
	struct ec_host_cmd_backend *backend_uart;
	struct ec_host_cmd_rx_ctx rx_ctx;
	struct ec_host_cmd_tx_buf tx;
	uint8_t rx_buf, tx_buf;

	backend_uart = ec_host_cmd_backend_get_uart(
		DEVICE_DT_GET(DT_CHOSEN(zephyr_host_cmd_uart_backend)));
	rx_ctx.buf = &rx_buf;
	rx_ctx.len_max = CONFIG_EC_HOST_CMD_HANDLER_RX_BUFFER_SIZE;
	tx.buf = &tx_buf;
	tx.len_max = CONFIG_EC_HOST_CMD_HANDLER_TX_BUFFER_SIZE;

	/* The UART backend init adjusts max response/request size. */
	backend_uart->api->init(backend_uart, &rx_ctx, &tx);

	/* Make sure max buffers sizes have been set correctly by the backend
	 * init. */
	zassert_equal(expected->max_request_packet_size, rx_ctx.len_max);
	zassert_equal(expected->max_response_packet_size, tx.len_max);
}
