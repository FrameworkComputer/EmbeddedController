/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "event_log.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "usb_pd.h"

/**
 * @brief This is the maximum size of a single log entry.
 *
 * Each entry must contain some common data + up to 16 bytes of additional type
 * specific data.
 */
#define MAX_EVENT_LOG_ENTRY_SIZE (sizeof(struct event_log_entry) + 16)

/**
 * @brief The size of the PD log entry data
 *
 * Logs from the PD include an additional 8 bytes of data to be sent to the AP.
 */
#define PD_LOG_ENTRY_DATA_SIZE (8)

struct pd_log_fixture {
	union {
		uint8_t event_log_buffer[MAX_EVENT_LOG_ENTRY_SIZE];
		struct event_log_entry log_entry;
	};
};

static void *pd_log_setup(void)
{
	static struct pd_log_fixture fixture;

	return &fixture;
}

static void pd_log_before(void *f)
{
	struct pd_log_fixture *fixture = f;

	while (log_dequeue_event(&fixture->log_entry) != 0) {
		if (fixture->log_entry.type == EVENT_LOG_NO_ENTRY) {
			break;
		}
	}
}

ZTEST_SUITE(pd_log, drivers_predicate_post_main, pd_log_setup, pd_log_before,
	    NULL, NULL);

ZTEST_USER(pd_log, test_bad_type)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_ACC_BASE,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PD_WRITE_LOG_ENTRY, UINT8_C(0), params);

	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&args), NULL);
}

ZTEST_USER(pd_log, test_bad_port)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_MCU_BASE,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PD_WRITE_LOG_ENTRY, UINT8_C(0), params);

	params.port = board_get_usb_pd_port_count() + 1;
	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&args), NULL);
}

ZTEST_USER_F(pd_log, test_mcu_charge)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_MCU_CHARGE,
		.port = 0,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PD_WRITE_LOG_ENTRY, UINT8_C(0), params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(sizeof(struct event_log_entry) + PD_LOG_ENTRY_DATA_SIZE,
		      log_dequeue_event(&fixture->log_entry), NULL);
	zassert_equal(params.type, fixture->log_entry.type, NULL);
	zassert_equal(PD_LOG_ENTRY_DATA_SIZE, fixture->log_entry.size, NULL);
	zassert_equal(0, fixture->log_entry.data, NULL);
	zassert_within(0, (int64_t)fixture->log_entry.timestamp, 10,
		       "Expected timestamp %" PRIi64
		       " to be within 10 ms of now",
		       (int64_t)fixture->log_entry.timestamp);
}
ZTEST_USER_F(pd_log, test_mcu_connect)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_MCU_CONNECT,
		.port = 0,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PD_WRITE_LOG_ENTRY, UINT8_C(0), params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(sizeof(struct event_log_entry),
		      log_dequeue_event(&fixture->log_entry), NULL);
	zassert_equal(params.type, fixture->log_entry.type, NULL);
	zassert_equal(0, fixture->log_entry.size, NULL);
	zassert_equal(0, fixture->log_entry.data, NULL);
	zassert_within(0, (int64_t)fixture->log_entry.timestamp, 10,
		       "Expected timestamp %" PRIi64
		       " to be within 10 ms of now",
		       (int64_t)fixture->log_entry.timestamp);
}

ZTEST_USER_F(pd_log, test_read_log_entry)
{
	uint8_t response_buffer[sizeof(struct ec_response_pd_log) + 16];
	struct ec_response_pd_log *response =
		(struct ec_response_pd_log *)response_buffer;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PD_GET_LOG_ENTRY, UINT8_C(0));

	args.response = response;
	args.response_max = sizeof(response_buffer);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(sizeof(struct event_log_entry), args.response_size, NULL);
	zassert_equal(PD_EVENT_NO_ENTRY, response->type, NULL);
}
