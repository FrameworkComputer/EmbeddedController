/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "host_command.h"
#include "task.h"

#include <zephyr/ztest.h>

#ifndef CONFIG_EC_HOST_CMD

static bool response_sent;

static void mock_send_response(struct host_packet *pkt)
{
	response_sent = true;
}

static uint8_t req_buf[100] = { 0 };
static uint8_t resp_buf[100] = { 0 };

ZTEST(host_cmd_host_commands, test_hc_processing_busy)
{
	struct ec_host_request *req = (struct ec_host_request *)req_buf;
	struct ec_host_response *resp = (struct ec_host_response *)resp_buf;
	int csum = 0;

	response_sent = false;

	req->struct_version = 3;
	req->checksum = 0;
	req->command = EC_CMD_HELLO;
	req->command_version = 0;
	req->reserved = 0;
	req->data_len = 0;

	for (int i = 0; i < sizeof(struct ec_host_request); ++i) {
		csum += req_buf[i];
	}
	req->checksum = (uint8_t)(-csum);

	struct host_packet pkt = {
		.send_response = mock_send_response,
		.request = req_buf,
		.request_temp = NULL,
		.request_max = sizeof(req_buf),
		.request_size = sizeof(struct ec_host_request),
		.response = resp_buf,
		.response_max = sizeof(resp_buf),
		.driver_result = 0,
	};

	/* First call should trigger busy processing flag */
	host_packet_receive(&pkt);

	/* Second call should return EC_RES_BUSY via driver_result */
	struct host_packet pkt2 = pkt;
	host_packet_receive(&pkt2);
	zassert_equal(pkt2.driver_result, EC_RES_BUSY,
		      "Expected EC_RES_BUSY but got %d", pkt2.driver_result);

	/* Wait for the first packet to be processed by the hostcmd thread */
	k_sleep(K_MSEC(10));

	/* Verify the first packet was actually processed successfully */
	zassert_true(response_sent,
		     "Response was never sent for the first packet");
	zassert_equal(
		resp->result, EC_RES_SUCCESS,
		"First packet did not complete successfully, got result %d",
		resp->result);
}
#endif
