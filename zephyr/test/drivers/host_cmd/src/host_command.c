/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "uart.h"

#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(host_cmd_host_commands, test_get_command_versions__v1)
{
	struct ec_response_get_cmd_versions response;
	struct ec_params_get_cmd_versions_v1 params = {
		.cmd = EC_CMD_GET_CMD_VERSIONS
	};
	int rv;

	rv = ec_cmd_get_cmd_versions_v1(NULL, &params, &response);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(EC_VER_MASK(0) | EC_VER_MASK(1), response.version_mask);
}

ZTEST(host_cmd_host_commands, test_get_command_versions__invalid_cmd)
{
	struct ec_response_get_cmd_versions response;
	struct ec_params_get_cmd_versions_v1 params = {
		/* Host command doesn't exist */
		.cmd = UINT16_MAX,
	};
	int rv;

	rv = ec_cmd_get_cmd_versions_v1(NULL, &params, &response);

	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_host_commands, test_get_comms_status)
{
	struct ec_response_get_comms_status response;
	int rv;

	rv = ec_cmd_get_comms_status(NULL, &response);

	zassert_ok(rv, "Got %d", rv);

	/* Unit test host commands are processed synchronously, so always expect
	 * the EC to be not busy processing another.
	 */
	zassert_false(response.flags);
}

#ifndef CONFIG_EC_HOST_CMD
ZTEST(host_cmd_host_commands, test_resend_response)
{
	struct host_cmd_handler_args args =
		(struct host_cmd_handler_args)BUILD_HOST_COMMAND_SIMPLE(
			EC_CMD_RESEND_RESPONSE, 0);
	int rv;

	rv = host_command_process(&args);
	zassert_ok(rv);

	/* The way we trigger host commands in tests doesn't cause results to
	 * get saved (it happens outside of host_command_process), so we cannot
	 * verify the resent response itself.
	 *
	 * TODO: test at least one host command through the ESPI interface.
	 */
}

#else
ZTEST(host_cmd_host_commands, test_resend_response)
{
	struct host_cmd_handler_args args =
		(struct host_cmd_handler_args)BUILD_HOST_COMMAND_SIMPLE(
			EC_CMD_RESEND_RESPONSE, 0);
	int rv;

	/* Send invalid erase parameters not to corrupt flash */
	struct ec_params_flash_erase erase_params = {
		.offset = 0x10000,
		.size = 0,
	};

	struct host_cmd_handler_args erase_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_FLASH_ERASE, 0, erase_params);

	rv = host_command_process(&erase_args);

	zassert_equal(EC_RES_IN_PROGRESS, rv);

	/* Give time for handler that returns IN_PROGRESS to execute. It runs
	 * in sysworkq
	 */
	k_sleep(K_MSEC(100));
	/* Expect error because of incorrect parameters - size = 0 */
	rv = host_command_process(&args);
	zassert_equal(EC_RES_ERROR, rv);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_UNAVAILABLE, rv);
}
#endif

ZTEST(host_cmd_host_commands, test_get_proto_version)
{
	struct ec_response_proto_version response;
	int rv;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_PROTO_VERSION, 0, response);

	rv = host_command_process(&args);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(EC_PROTO_VERSION, response.version);
}

ZTEST(host_cmd_host_commands, test_hello)
{
	struct ec_response_hello response;
	struct ec_params_hello params;
	int rv;
	uint32_t params_to_test[] = { 0x0, 0xaaaaaaaa, 0xffffffff };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);

	for (int i = 0; i < ARRAY_SIZE(params_to_test); i++) {
		params.in_data = params_to_test[i];

		rv = host_command_process(&args);

		zassert_ok(rv, "Got %d, in_data: %x", rv, params_to_test[i]);
		zassert_equal(args.response_size, sizeof(response));
		zassert_equal(params_to_test[i] + 0x01020304, response.out_data,
			      "in_data: %x", params_to_test[i]);
	}
}

ZTEST(host_cmd_host_commands, test_ap_fw_state)
{
	const struct shell *shell_zephyr = get_ec_shell();
	struct ec_params_ap_fw_state params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_AP_FW_STATE, 0, params);
	const char *outbuffer;
	size_t buffer_size;
	int rv;

	/* Flush the console buffer before we start. */
	shell_backend_dummy_clear_output(shell_zephyr);

	/* Test 0: Verify console command shows no state initially */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "No AP fw state") != NULL,
		     "Initial state not empty: %s", outbuffer);

	/* Test 1: Save first state */
	params.state = 0x12345678;
	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "AP_FW 12345678") != NULL,
		     "Invalid console output %s", outbuffer);

	/* Test 2: Verify console command shows it */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "AP_FW 12345678") != NULL,
		     "Console command output missing state: %s", outbuffer);

	/* Test 3: Save second state (should overwrite) */
	params.state = 0x87654321;
	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	/* Test 4: Verify console command shows new state */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "AP_FW 87654321") != NULL,
		     "Console command output missing new state: %s", outbuffer);

	/* Test 5: Clear buffer (silent) */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens clear"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	/* Verify it's empty now */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "No AP fw state") != NULL,
		     "Buffer not empty after clear! Output: %s", outbuffer);
}

#define TEST_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(extra_test_stack, TEST_STACK_SIZE);
struct k_thread extra_test_thread;
K_SEM_DEFINE(test_sem, 0, 1);

static void extra_test_thread_entry(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sem_take(&test_sem, K_MSEC(100));
	}
}

ZTEST(host_cmd_host_commands, test_thread_info)
{
	k_tid_t tid;
	struct ec_response_thread_info_list list;
	struct host_cmd_handler_args list_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_THREAD_INFO_LIST, 0, list);
	bool found_idle = false;
	bool found_current = false;
	bool found_test_thread = false;

	/* Create an extra thread */
	tid = k_thread_create(&extra_test_thread, extra_test_stack,
			      TEST_STACK_SIZE, extra_test_thread_entry, NULL,
			      NULL, NULL, 1, K_INHERIT_PERMS, K_NO_WAIT);
	k_thread_name_set(tid, "extra_thread");

	/* Wait for thread to start and sleep */
	k_sleep(K_MSEC(10));

	/* Call list command */
	zassert_equal(EC_RES_SUCCESS, host_command_process(&list_args));
	zassert_true(list.thread_count > 0);

	/* Verify threads */
	for (uint32_t i = 0; i < list.thread_count; i++) {
		struct ec_params_thread_info_detail p;
		struct ec_response_thread_info_detail r;
		struct host_cmd_handler_args detail_args =
			BUILD_HOST_COMMAND(EC_CMD_THREAD_INFO_DETAIL, 0, r, p);

		p.thread_id = list.thread_ids[i];
		zassert_equal(EC_RES_SUCCESS,
			      host_command_process(&detail_args));
		zassert_true(r.timestamp_us > 0, "Timestamp is 0");

		if (r.is_idle) {
			found_idle = true;
		}
		if (r.is_current) {
			found_current = true;
		}

		/* Check that flags are valid and value is != 0 for unchecked
		 * fields */
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_RUNTIME_USAGE_VALID) {
			zassert_true(r.execution_time_us != 0,
				     "Execution time is 0");
		}
		if (r.valid_flags &
		    EC_THREAD_INFO_DETAIL_USAGE_ANALYSIS_VALID) {
			zassert_true(r.window_peak_us != 0, "Window peak is 0");
			zassert_true(r.window_avg_us != 0, "Window avg is 0");
		}
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_PC_VALID) {
			zassert_true(r.pc != 0, "PC is 0");
		}
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_LR_VALID) {
			zassert_true(r.lr != 0, "LR is 0");
		}
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_SP_VALID) {
			zassert_true(r.sp != 0, "SP is 0");
		}

		/* New checks for all threads */
		zassert_true(r.entry_point > 0, "Entry point is 0");
		/* pending_on might be 0 if not blocked, so do not assert it
		 * here */
		/* Check thread_state is not dead */
		zassert_true(!(r.thread_state & 8), "Thread is dead");

		if (p.thread_id == (uint32_t)(uintptr_t)tid) {
			found_test_thread = true;
			/* Verify details of our test thread */
			zassert_true(r.valid_flags &
				     EC_THREAD_INFO_DETAIL_NAME_VALID);
			zassert_str_equal(r.name, "extra_thread");
			zassert_equal(r.prio, 1);
			zassert_equal(r.stack_size, TEST_STACK_SIZE);
			zassert_true(r.valid_flags &
				     EC_THREAD_INFO_DETAIL_STACK_VALID);
			zassert_true(r.stack_max <= r.stack_size);
			zassert_true(r.stack_cur <= r.stack_size);

			zassert_true(r.user_options & K_INHERIT_PERMS,
				     "K_INHERIT_PERMS option not set");
			zassert_true(r.timeout_us > 0, "Timeout is 0");
			zassert_true(r.timeout_us != 0xffffffff,
				     "Timeout is forever");
			zassert_equal(r.pending_on,
				      (uint32_t)(uintptr_t)&test_sem,
				      "Pending on wrong object");

			/* Additional checks for test thread */
			zassert_true(r.thread_state & 2,
				     "Thread is not pending");
		}
	}
	zassert_true(found_idle, "No idle thread found");
	zassert_true(found_current, "No current thread found");
	zassert_true(found_test_thread, "Extra test thread not found in list");

	/* Clean up thread */
	k_thread_abort(tid);
}

ZTEST_SUITE(host_cmd_host_commands, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
