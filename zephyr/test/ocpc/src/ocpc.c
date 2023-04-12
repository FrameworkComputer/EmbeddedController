/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "host_command.h"
#include "ocpc.h"

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(ocpc_get_pid_constants, int *, int *, int *, int *, int *,
	       int *);

static int test_kp, test_kp_div, test_ki, test_ki_div, test_kd, test_kd_div;

static void get_pid_constants_custom_fake(int *kp, int *kp_div, int *ki,
					  int *ki_div, int *kd, int *kd_div)
{
	*kp = test_kp;
	*kp_div = test_kp_div;
	*ki = test_ki;
	*ki_div = test_ki_div;
	*kd = test_kd;
	*kd_div = test_kd_div;
}

ZTEST_USER(ocpc, test_consolecmd_ocpcpid__read)
{
	const char *outbuffer;
	size_t buffer_size;

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for some expected lines */
	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Kp = 1 / 4"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Ki = 1 / 15"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Kd = 1 / 10"), "Output was: `%s`",
		   outbuffer);
}

ZTEST_USER(ocpc, test_consolecmd_ocpcpid__write)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Call a few times to change each parameter and examine output of final
	 * command.
	 */

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid p 2 3"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid i 4 5"));
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid d 6 7"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Kp = 2 / 3"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Ki = 4 / 5"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Kd = 6 / 7"), "Output was: `%s`",
		   outbuffer);
}

ZTEST_USER(ocpc, test_consolecmd_ocpcpid__bad_param)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "ocpcpid y 0 0"));
}

ZTEST_USER(ocpc, test_consolecmd_ocpcdrvlmt)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Set to 100mV */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdrvlmt 100"));

	/* Read back and verify */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdrvlmt"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Drive Limit = 100"), "Output was: `%s`",
		   outbuffer);
}

ZTEST_USER(ocpc, test_consolecmd_ocpcdebug)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug ena"));
	zassert_true(test_ocpc_get_debug_output());
	zassert_false(test_ocpc_get_viz_output());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug dis"));
	zassert_false(test_ocpc_get_debug_output());
	zassert_false(test_ocpc_get_viz_output());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug viz"));
	zassert_false(test_ocpc_get_debug_output());
	zassert_true(test_ocpc_get_viz_output());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug all"));
	zassert_true(test_ocpc_get_debug_output());
	zassert_true(test_ocpc_get_viz_output());

	/* Bad param */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "ocpcdebug foo"));

	/* Missing param */
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "ocpcdebug"));
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Reset */
	RESET_FAKE(ocpc_get_pid_constants);

	/* Load values that match ocpc.c's defaults */
	test_kp = 1;
	test_kp_div = 4;
	test_ki = 1;
	test_ki_div = 15;
	test_kd = 1;
	test_kd_div = 10;

	ocpc_get_pid_constants_fake.custom_fake = get_pid_constants_custom_fake;

	/* Force an update which will use the above parameters. */
	ocpc_set_pid_constants();
}

ZTEST_SUITE(ocpc, NULL, NULL, reset, reset, NULL);
