/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test host command.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

/* Request/response buffer size (and maximum command length) */
#define BUFFER_SIZE 128

struct host_packet pkt;
static char resp_buf[BUFFER_SIZE];
static char req_buf[BUFFER_SIZE + 4];
struct ec_host_request *req = (struct ec_host_request *)req_buf;
struct ec_params_hello *p = (struct ec_params_hello *)(req_buf + sizeof(*req));
struct ec_host_response *resp = (struct ec_host_response *)resp_buf;
struct ec_response_hello *r =
	(struct ec_response_hello *)(resp_buf + sizeof(*resp));

static void hostcmd_respond(struct host_packet *pkt)
{
	task_wake(TASK_ID_TEST_RUNNER);
}

static char calculate_checksum(const char *buf, int size)
{
	int c = 0;
	int i;

	for (i = 0; i < size; ++i)
		c += buf[i];

	return -c;
}

static void hostcmd_send(void)
{
	req->checksum = calculate_checksum(req_buf, pkt.request_size);
	host_packet_receive(&pkt);
	task_wait_event(-1);
}

static void hostcmd_fill_in_default(void)
{
	req->struct_version = 3;
	req->checksum = 0;
	req->command = EC_CMD_HELLO;
	req->command_version = 0;
	req->reserved = 0;
	req->data_len = 4;
	p->in_data = 0x11223344;

	pkt.request_size = 0;
	pkt.send_response = hostcmd_respond;
	pkt.request = (const void *)req_buf;
	pkt.request_max = BUFFER_SIZE;
	pkt.request_size = sizeof(*req) + sizeof(*p);
	pkt.response = (void *)resp_buf;
	pkt.response_max = BUFFER_SIZE;
	pkt.driver_result = 0;
}

static int test_hostcmd_ok(void)
{
	hostcmd_fill_in_default();

	hostcmd_send();

	TEST_ASSERT(calculate_checksum(resp_buf,
				       sizeof(*resp) + resp->data_len) == 0);
	TEST_ASSERT(resp->result == EC_RES_SUCCESS);
	TEST_ASSERT(r->out_data == 0x12243648);

	return EC_SUCCESS;
}

static int test_hostcmd_too_short(void)
{
	hostcmd_fill_in_default();

	/* Smaller than header */
	pkt.request_size = sizeof(*req) - 4;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_REQUEST_TRUNCATED);

	/* Smaller than expected data size */
	pkt.request_size = sizeof(*req);
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_REQUEST_TRUNCATED);

	return EC_SUCCESS;
}

static int test_hostcmd_too_long(void)
{
	hostcmd_fill_in_default();

	/* Larger than request buffer */
	pkt.request_size = BUFFER_SIZE + 4;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_REQUEST_TRUNCATED);

	return EC_SUCCESS;
}

static int test_hostcmd_driver_error(void)
{
	hostcmd_fill_in_default();

	pkt.driver_result = EC_RES_ERROR;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_ERROR);

	return EC_SUCCESS;
}

static int test_hostcmd_invalid_command(void)
{
	hostcmd_fill_in_default();

	req->command = 0xff;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_INVALID_COMMAND);

	return EC_SUCCESS;
}

static int test_hostcmd_wrong_command_version(void)
{
	hostcmd_fill_in_default();

	req->command_version = 1;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_INVALID_VERSION);

	return EC_SUCCESS;
}

static int test_hostcmd_wrong_struct_version(void)
{
	hostcmd_fill_in_default();

	req->struct_version = 4;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_INVALID_HEADER);

	req->struct_version = 2;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_INVALID_HEADER);

	return EC_SUCCESS;
}

static int test_hostcmd_invalid_checksum(void)
{
	hostcmd_fill_in_default();

	req->checksum++;
	hostcmd_send();
	TEST_ASSERT(resp->result == EC_RES_INVALID_CHECKSUM);

	return EC_SUCCESS;
}

void run_test(void)
{
	wait_for_task_started();
	test_reset();

	RUN_TEST(test_hostcmd_ok);
	RUN_TEST(test_hostcmd_too_short);
	RUN_TEST(test_hostcmd_too_long);
	RUN_TEST(test_hostcmd_driver_error);
	RUN_TEST(test_hostcmd_invalid_command);
	RUN_TEST(test_hostcmd_wrong_command_version);
	RUN_TEST(test_hostcmd_wrong_struct_version);
	RUN_TEST(test_hostcmd_invalid_checksum);

	test_print_result();
}
