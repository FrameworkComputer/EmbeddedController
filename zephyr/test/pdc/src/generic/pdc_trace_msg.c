/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "drivers/pdc.h"
#include "host_command.h"
#include "usbc/pdc_trace_msg.h"

#include <stdint.h>

#include <zephyr/ztest.h>

#define TEST_PORT 0

#define SEQ_NUM_BITS (8 * member_size(struct pdc_trace_msg_entry, seq_num))
#define SEQ_NUM_MOD(n) ((n) & ((1 << SEQ_NUM_BITS) - 1))

/*
 * This is the default size in the real implementation
 */
static const int msg_fifo_size_log2 = 10;
static const int msg_fifo_size = 1 << msg_fifo_size_log2;

static void pdc_trace_msg_before_test(void *data)
{
	/*
	 * Tracing is typically off by default, let's make sure.
	 */
	pdc_trace_msg_enable(EC_PDC_TRACE_MSG_PORT_NONE);
	pdc_trace_msg_fifo_reset();
}

ZTEST_SUITE(pdc_trace_msg, NULL, NULL, pdc_trace_msg_before_test, NULL, NULL);

ZTEST_USER(pdc_trace_msg, test_enable_for_port)
{
	int status;

	status = pdc_trace_msg_enable(EC_PDC_TRACE_MSG_PORT_ALL);
	zassert_equal(status, EC_PDC_TRACE_MSG_PORT_NONE,
		      "expected %d but got %d", EC_PDC_TRACE_MSG_PORT_NONE,
		      status);

	status = pdc_trace_msg_enable(TEST_PORT);
	zassert_equal(status, EC_PDC_TRACE_MSG_PORT_ALL, "expected %d, got %d",
		      EC_PDC_TRACE_MSG_PORT_ALL, status);

	status = pdc_trace_msg_enable(EC_PDC_TRACE_MSG_PORT_NONE);
	zassert_equal(status, TEST_PORT, "expected %d, got %d", TEST_PORT,
		      status);

	status = pdc_trace_msg_enable(EC_PDC_TRACE_MSG_PORT_NONE);
	zassert_equal(status, (EC_PDC_TRACE_MSG_PORT_NONE),
		      "expected %d, got %d", (EC_PDC_TRACE_MSG_PORT_NONE),
		      status);
}

static int hc_msg_enable(struct ec_response_pdc_trace_msg_enable *r)
{
	struct ec_params_pdc_trace_msg_enable msg_enable_p = {
		.port = TEST_PORT,
	};
	struct host_cmd_handler_args msg_enable_args = BUILD_HOST_COMMAND(
		EC_CMD_PDC_TRACE_MSG_ENABLE, 0, *r, msg_enable_p);

	return host_command_process(&msg_enable_args);
}

static int hc_msg_get(struct ec_response_pdc_trace_msg_get_entries *r)
{
	struct host_cmd_handler_args msg_get_args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_PDC_TRACE_MSG_GET_ENTRIES, 0, *r);

	return host_command_process(&msg_get_args);
}

static int walk_pl(const uint8_t *const pl, const int pl_size)
{
	const struct pdc_trace_msg_entry *e;

	int bytes_processed = 0;
	int n_messages = 0;
	int exp_seq_num = -1;

	do {
		int e_size;

		zassert_true(
			bytes_processed + sizeof(struct pdc_trace_msg_entry) <=
				pl_size,
			"partial pdc_trace_msg_entry in payload at offset %d of %d",
			bytes_processed, pl_size);

		e = (const struct pdc_trace_msg_entry *)&pl[bytes_processed];

		if (exp_seq_num == -1) {
			exp_seq_num = e->seq_num;
		} else {
			zassert_equal(e->seq_num, exp_seq_num,
				      "got seq_num %d instead of %d",
				      e->seq_num, exp_seq_num);
		}
		zassert_equal(e->port_num, TEST_PORT,
			      "got port_num %d instead of %d", e->port_num,
			      TEST_PORT);
		zassert_equal(e->msg_type, PDC_TRACE_CHIP_TYPE_RTS54XX,
			      "got msg_type %d instead of %d", e->msg_type,
			      PDC_TRACE_CHIP_TYPE_RTS54XX);
		e_size = e->pdc_data_size;
		zassert_not_equal(e_size, 0, "got empty entry");
		e_size += sizeof(struct pdc_trace_msg_entry);
		zassert_true(bytes_processed + e_size <= pl_size,
			     "entry sizes exceed buffer");
		bytes_processed += e_size;
		exp_seq_num = SEQ_NUM_MOD(exp_seq_num + 1);
		++n_messages;
	} while (bytes_processed < pl_size);

	return n_messages;
}

/*
 * @brief Push a single message into FIFO.
 *        Message is filled with a test pattern.
 *
 * @param msg_bytes  Size of message
 * @param as_request Tag message as a request message, else response message
 *
 * @return true IFF message was written to FIFO.
 */
static bool push_msg(int msg_bytes, bool as_request)
{
	uint8_t msg[sizeof(struct pdc_trace_msg_entry) + msg_fifo_size];
	bool status;

	for (int i = 0; i < msg_bytes; ++i)
		msg[i] = (msg_bytes + i) & 0xff;

	if (as_request) {
		status = pdc_trace_msg_req(
			TEST_PORT, PDC_TRACE_CHIP_TYPE_RTS54XX, msg, msg_bytes);
	} else {
		status = pdc_trace_msg_resp(
			TEST_PORT, PDC_TRACE_CHIP_TYPE_RTS54XX, msg, msg_bytes);
	}

	return status;
}

/*
 * @brief Fill FIFO with incrementally larger messages until full.
 *
 * @return number of messages added to FIFO
 */

static int fill_fifo(void)
{
	int pl_bytes;

	for (pl_bytes = 1; pl_bytes < msg_fifo_size; ++pl_bytes) {
		bool status;

		/*
		 * _req vs. _resp are interchangeable for FIFO tests,
		 * so alternate between them.
		 */
		status = push_msg(pl_bytes, !!(pl_bytes & 0x01));

		if (!status)
			break;
	}
	zassert_not_equal(pl_bytes, msg_fifo_size,
			  "message FIFO did not report overflow condition");

	return pl_bytes - 1;
}

ZTEST_USER(pdc_trace_msg, test_fifo_ops)
{
	uint8_t res_buf[msg_fifo_size];
	struct ec_response_pdc_trace_msg_get_entries *r =
		(struct ec_response_pdc_trace_msg_get_entries *)res_buf;

	/*
	 * Push a message into the FIFO.
	 * It should not go in since tracing is disabled at this point.
	 */

	zassert_false(push_msg(101, true));

	/*
	 * The FIFO should be empty before enabling tracing.
	 */
	zassert_ok(hc_msg_get(r));
	zassert_equal(r->pl_size, 0, "initial pl_size %d but expected 0",
		      r->pl_size);

	push_msg(99, true);

	/*
	 * The FIFO should remain empty before enabling tracing.
	 */
	zassert_ok(hc_msg_get(r));
	zassert_equal(r->pl_size, 0,
		      "pl_size %d but expected 0 with tracing disabled",
		      r->pl_size);

	/*
	 * Enable tracing using the EC CLI which exercises the
	 * EC_CMD_PDC_TRACE_MSG_ENABLE host command and ultimately calls
	 * pdc_trace_msg_enable.
	 */
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "pdc trace " STRINGIFY(TEST_PORT)),
		   "could not run pdc trace " STRINGIFY(TEST_PORT));

	const int test_msg_in_size = 111;
	const int test_msg_out_size =
		sizeof(struct pdc_trace_msg_entry) + test_msg_in_size;
	push_msg(test_msg_in_size, true);

	/*
	 * Host command should retrieve the message from the FIFO with a
	 * header prepended.
	 */
	zassert_ok(hc_msg_get(r));
	zassert_equal(r->pl_size, test_msg_out_size,
		      "pl_size %d but expected %d with tracing enabled",
		      r->pl_size, test_msg_out_size);

	/*
	 * The FIFO should be empty again.
	 */
	zassert_ok(hc_msg_get(r));
	zassert_equal(r->pl_size, 0, "pl_size %d but expected 0 after draining",
		      r->pl_size);

	/*
	 * Note that since the FIFO is a circular buffer, we are implicitly
	 * testing the wrap-around case after adding the first entry.
	 */

	const int msg_count = fill_fifo();

	/*
	 * Verify the FIFO drop count incremented by one.
	 */
	struct ec_response_pdc_trace_msg_enable msg_enable_r;
	zassert_ok(hc_msg_enable(&msg_enable_r));
	zassert_equal(msg_enable_r.dropped_count, 1,
		      "expected drop count 1 but got %d",
		      msg_enable_r.dropped_count);

	int returned_messages = 0;
	int msg;

	/*
	 * Returned messages may be batched.
	 */
	for (msg = 0; msg < msg_count; ++msg) {
		int n_messages;

		zassert_ok(hc_msg_get(r));
		if (r->pl_size == 0)
			break;
		n_messages = walk_pl(r->payload, r->pl_size);
		returned_messages += n_messages;
	}

	/*
	 * The FIFO should now be empty.
	 */
	zassert_ok(hc_msg_get(r));
	zassert_equal(r->pl_size, 0, "got pl_size %d but expected 0",
		      r->pl_size);

	/*
	 * Did we receive all the messages we sent?
	 */
	zassert_equal(returned_messages, msg_count,
		      "got %d messages but expected %d", returned_messages,
		      msg_count);
}

ZTEST_USER(pdc_trace_msg, test_console_cmd_syntax)
{
	char cmd_buf[100];

	zassert_not_ok(
		shell_execute_cmd(get_ec_shell(), "pdc trace 0z"),
		"pdc trace should have rejected malformed port number \"0z\"");

	zassert_not_ok(
		shell_execute_cmd(get_ec_shell(), "pdc trace -1"),
		"pdc trace should have rejected invalid port number \"-1\"");

	snprintf(cmd_buf, sizeof(cmd_buf), "pdc trace %d",
		 EC_PDC_TRACE_MSG_PORT_NONE);
	zassert_not_ok(
		shell_execute_cmd(get_ec_shell(), cmd_buf),
		"pdc trace should have rejected reserved port number \"%d\"",
		EC_PDC_TRACE_MSG_PORT_NONE);

	snprintf(cmd_buf, sizeof(cmd_buf), "pdc trace %d",
		 EC_PDC_TRACE_MSG_PORT_ALL);
	zassert_not_ok(
		shell_execute_cmd(get_ec_shell(), cmd_buf),
		"pdc trace should have rejected reserved port number \"%d\"",
		EC_PDC_TRACE_MSG_PORT_ALL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "pdc trace on"), NULL);

	/*
	 * Add a message to the FIFO to improve coverage of the
	 * console command.
	 */
	zassert_true(push_msg(99, true));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "pdc trace"),
		   "could not run pdc trace");

	zassert_ok(shell_execute_cmd(get_ec_shell(), "pdc trace off"), NULL);
}
