/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fuzz host command.
 */

#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "host_test.h"
#include "printf.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <pthread.h>
#include <sys/time.h>

/* Only test requests with valid size and checksum (makes fuzzing faster) */
#define VALID_REQUEST_ONLY

#define TASK_EVENT_FUZZ TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_HOSTCMD_DONE TASK_EVENT_CUSTOM_BIT(1)

/*
 * Request/response buffer size (and maximum command length).
 * See comments in libec/ec_command.h:
 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/libec/ec_command.h;l=43-58;drc=2d8cc86c157e60b412f82bc536e542fd1c164ccb
 */
#define BUFFER_SIZE 544

struct host_packet pkt;
static uint8_t resp_buf[BUFFER_SIZE];
struct ec_host_response *resp = (struct ec_host_response *)resp_buf;
static uint8_t req_buf[BUFFER_SIZE];
static struct ec_host_request *req = (struct ec_host_request *)req_buf;

static void hostcmd_respond(struct host_packet *pkt)
{
	task_set_event(TASK_ID_TEST_RUNNER, TASK_EVENT_HOSTCMD_DONE);
}

static char calculate_checksum(const char *buf, int size)
{
	int c = 0;
	int i;

	for (i = 0; i < size; ++i)
		c += buf[i];

	return -c;
}

struct chunk {
	int start;
	int size;
};

static int hostcmd_fill(const uint8_t *data, size_t size)
{
	static int first = 1;
	int ipos = 0;
	int i;
	int req_size = 0;

#ifdef VALID_REQUEST_ONLY
	const int checksum_offset = offsetof(struct ec_host_request, checksum);
	const int checksum_size = sizeof(req->checksum);
	const int data_len_offset = offsetof(struct ec_host_request, data_len);
	const int data_len_size = sizeof(req->data_len);

	struct chunk chunks[3];

	chunks[0].start = 0;
	chunks[0].size = checksum_offset;
	chunks[1].start = chunks[0].start + chunks[0].size + checksum_size;
	chunks[1].size = data_len_offset - chunks[1].start;
	chunks[2].start = chunks[1].start + chunks[1].size + data_len_size;
	chunks[2].size = sizeof(req_buf) - chunks[2].start;
#else
	struct chunk chunks[1] = { { 0, sizeof(req_buf) } };
#endif

	/*
	 * TODO(crbug.com/172212308): We should probably malloc req_buf with the
	 * correct size, to make we do not read uninitialized req_buf data.
	 */
	memset(req_buf, 0, sizeof(req_buf));

	/*
	 * Fill in req_buf, according to chunks defined above (i.e. skipping
	 * over checksum and data_len.
	 */
	for (i = 0; i < ARRAY_SIZE(chunks) && ipos < size; i++) {
		int cp_size = MIN(chunks[i].size, size - ipos);

		memcpy(req_buf + chunks[i].start, data + ipos, cp_size);

		ipos += cp_size;

		req_size = chunks[i].start + cp_size;
	}

	/* Not enough space in req_buf. */
	if (ipos != size)
		return -1;

	pkt.request_size = req_size;
	req->data_len = req_size - sizeof(*req);
	req->checksum = calculate_checksum(req_buf, req_size);

	/*
	 * Print the full request on the first fuzzing attempt: useful to
	 * report bugs, and write up commit messages when reproducing
	 * issues.
	 */
	if (first) {
		char str_buf[hex_str_buf_size(req_size)];

		snprintf_hex_buffer(str_buf, sizeof(str_buf),
				    HEX_BUF(req_buf, req_size));
		ccprintf("Request: cmd=%04x data=%s\n", req->command, str_buf);
		first = 0;
	}

	pkt.send_response = hostcmd_respond;
	pkt.request = (const void *)req_buf;
	pkt.request_max = BUFFER_SIZE;
	pkt.response = (void *)resp_buf;
	pkt.response_max = BUFFER_SIZE;
	pkt.driver_result = 0;

	return 0;
}

static pthread_cond_t done_cond;
static pthread_mutex_t lock;

void run_test(int argc, const char **argv)
{
	ccprints("Fuzzing task started");
	wait_for_task_started();

	while (1) {
		task_wait_event_mask(TASK_EVENT_FUZZ, -1);
		/* Send the host command (pkt prepared by main thread). */
		host_packet_receive(&pkt);
		task_wait_event_mask(TASK_EVENT_HOSTCMD_DONE, -1);
		pthread_cond_signal(&done_cond);
	}
}

int test_fuzz_one_input(const uint8_t *data, unsigned int size)
{
	/* Fill in req_buf. */
	if (hostcmd_fill(data, size) < 0)
		return 0;

	task_set_event(TASK_ID_TEST_RUNNER, TASK_EVENT_FUZZ);
	pthread_cond_wait(&done_cond, &lock);

#ifdef VALID_REQUEST_ONLY
	/*
	 * We carefully crafted all our requests to have a valid checksum, so
	 * we should never receive an invalid checksum error. (but ignore
	 * EC_CMD_TEST_PROTOCOL, as it can lead to arbitrary result values).
	 */
	ASSERT(req->command == EC_CMD_TEST_PROTOCOL ||
	       resp->result != EC_RES_INVALID_CHECKSUM);
#endif

	return 0;
}
