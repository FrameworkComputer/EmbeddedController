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
struct ec_response_get_chip_info *chip_info_r =
	(struct ec_response_get_chip_info *)(resp_buf + sizeof(*resp));

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

	pkt.send_response = hostcmd_respond;
	pkt.request = (const void *)req_buf;
	pkt.request_temp = NULL;
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

static int test_hostcmd_reuse_response_buffer(void)
{
	struct ec_host_request *h = (struct ec_host_request *)resp_buf;
	struct ec_params_hello *d =
		(struct ec_params_hello *)(resp_buf + sizeof(*h));

	h->struct_version = 3;
	h->checksum = 0;
	h->command = EC_CMD_HELLO;
	h->command_version = 0;
	h->reserved = 0;
	h->data_len = 4;
	d->in_data = 0x11223344;

	pkt.send_response = hostcmd_respond;
	/*
	 * The original request buffer is shared with the response and the
	 * request buffer is used as the temporary buffer
	 */
	pkt.request = (const void *)resp_buf;
	pkt.request_temp = req_buf;
	pkt.request_max = BUFFER_SIZE;
	pkt.request_size = sizeof(*h) + sizeof(*d);
	pkt.response = (void *)resp_buf;
	pkt.response_max = BUFFER_SIZE;
	pkt.driver_result = 0;

	h->checksum = calculate_checksum(resp_buf, pkt.request_size);

	ccprintf("\nBuffer contents before process 0x%ph\n",
			HEX_BUF(resp_buf, BUFFER_SIZE));
	host_packet_receive(&pkt);
	task_wait_event(-1);

	ccprintf("\nBuffer contents after process 0x%ph\n",
			HEX_BUF(resp_buf, BUFFER_SIZE));

	TEST_EQ(calculate_checksum(resp_buf,
				sizeof(*resp) + resp->data_len), 0, "%d");
	TEST_EQ(resp->result, EC_RES_SUCCESS, "%d");
	TEST_EQ(r->out_data, 0x12243648, "0x%x");

	return EC_SUCCESS;
}

static void hostcmd_fill_chip_info(void)
{
	req->struct_version = 3;
	req->checksum = 0;
	req->command = EC_CMD_GET_CHIP_INFO;
	req->command_version = 0;
	req->reserved = 0;
	req->data_len = 0;

	pkt.send_response = hostcmd_respond;
	pkt.request = (const void *)req_buf;
	pkt.request_temp = NULL;
	pkt.request_max = BUFFER_SIZE;
	pkt.request_size = sizeof(*req);
	pkt.response = (void *)resp_buf;
	pkt.response_max = BUFFER_SIZE;
	pkt.driver_result = 0;
}

static int test_hostcmd_clears_unused_data(void)
{
	int i, found_null;

	/* Set the buffer to junk and ensure that is gets cleared */
	memset(resp_buf, 0xAA, BUFFER_SIZE);
	hostcmd_fill_chip_info();

	hostcmd_send();

	ccprintf("\nBuffer contents 0x%ph\n",
			HEX_BUF(resp_buf, BUFFER_SIZE));

	TEST_EQ(calculate_checksum(resp_buf,
				sizeof(*resp) + resp->data_len), 0, "%d");
	TEST_EQ(resp->result, EC_RES_SUCCESS, "%d");

	/* Ensure partial strings have 0s after the NULL byte */
	found_null = 0;
	for (i = 0; i < sizeof(chip_info_r->name); ++i) {
		if (!chip_info_r->name[i])
			found_null = 1;

		if (found_null) {
			if (chip_info_r->name[i])
				ccprintf("\nByte %d is not zero!\n", i);
			TEST_EQ(chip_info_r->name[i], 0, "0x%x");
		}
	}

	found_null = 0;
	for (i = 0; i < sizeof(chip_info_r->revision); ++i) {
		if (!chip_info_r->revision[i])
			found_null = 1;

		if (found_null) {
			if (chip_info_r->revision[i])
				ccprintf("\nByte %d is not zero!\n", i);
			TEST_EQ(chip_info_r->revision[i], 0, "0x%x");
		}
	}

	found_null = 0;
	for (i = 0; i < sizeof(chip_info_r->vendor); ++i) {
		if (!chip_info_r->vendor[i])
			found_null = 1;

		if (found_null) {
			if (chip_info_r->vendor[i])
				ccprintf("\nByte %d is not zero!\n", i);
			TEST_EQ(chip_info_r->vendor[i], 0, "0x%x");
		}
	}

	/* Ensure rest of buffer after valid response is also 0 */
	for (i = resp->data_len + sizeof(*resp) + 1; i < BUFFER_SIZE; ++i) {
		if (resp_buf[i])
			ccprintf("\nByte %d is not zero!\n", i);
		TEST_EQ(resp_buf[i], 0, "0x%x");
	}

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
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
	RUN_TEST(test_hostcmd_reuse_response_buffer);
	RUN_TEST(test_hostcmd_clears_unused_data);

	test_print_result();
}
