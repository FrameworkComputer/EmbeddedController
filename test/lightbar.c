/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "lightbar.h"
#include "host_command.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int get_seq(void)
{
	int rv;
	struct ec_params_lightbar params;
	struct ec_response_lightbar resp;

	/* Get the state */
	memset(&resp, 0, sizeof(resp));
	params.cmd = LIGHTBAR_CMD_GET_SEQ;
	rv = test_send_host_command(EC_CMD_LIGHTBAR_CMD, 0,
				    &params, sizeof(params),
				    &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d\n", __FILE__, __func__, rv);
		return -1;
	}

	return resp.get_seq.num;
}

static int set_seq(int s)
{
	int rv;
	struct ec_params_lightbar params;
	struct ec_response_lightbar resp;

	/* Get the state */
	memset(&resp, 0, sizeof(resp));
	params.cmd = LIGHTBAR_CMD_SEQ;
	params.seq.num = s;
	rv = test_send_host_command(EC_CMD_LIGHTBAR_CMD, 0,
				    &params, sizeof(params),
				    &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d\n", __FILE__, __func__, rv);
		return -1;
	}

	return EC_RES_SUCCESS;
}

static int test_double_oneshots(void)
{
	/* Start in S0 */
	TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S0);
	/* Invoke the oneshot */
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	/* Switch to a different oneshot while that one's running */
	TEST_ASSERT(set_seq(LIGHTBAR_KONAMI) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_KONAMI);
	/* Afterwards, it should go back to the original normal state */
	usleep(30 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S0);

	/* Same test, but with a bunch more oneshots. */
	TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S0);
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	TEST_ASSERT(set_seq(LIGHTBAR_KONAMI) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_KONAMI);
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	TEST_ASSERT(set_seq(LIGHTBAR_KONAMI) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_KONAMI);
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	/* It should still go back to the original normal state */
	usleep(30 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S0);

	/* But if the interruption is a normal state, that should stick. */
	TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S0);
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	TEST_ASSERT(set_seq(LIGHTBAR_KONAMI) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_KONAMI);
	/* Here's a normal sequence */
	TEST_ASSERT(set_seq(LIGHTBAR_S3) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S3);
	/* And another one-shot */
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	TEST_ASSERT(set_seq(LIGHTBAR_KONAMI) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_KONAMI);
	TEST_ASSERT(set_seq(LIGHTBAR_TAP) == EC_RES_SUCCESS);
	usleep(SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_TAP);
	/* It should go back to the new normal sequence */
	usleep(30 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S3);

	return EC_SUCCESS;
}

static int test_oneshots_norm_msg(void)
{
	/* Revert to the next state when interrupted with a normal message. */
	enum lightbar_sequence seqs[] = {
		LIGHTBAR_KONAMI,
		LIGHTBAR_TAP,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(seqs); i++) {
		/* Start in S0 */
		TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
		usleep(SECOND);
		/* Invoke the oneshot */
		TEST_ASSERT(set_seq(seqs[i]) == EC_RES_SUCCESS);
		usleep(SECOND);
		/* Interrupt with S0S3 */
		TEST_ASSERT(set_seq(LIGHTBAR_S0S3) == EC_RES_SUCCESS);
		usleep(SECOND);
		/* It should be back right away */
		TEST_ASSERT(get_seq() == LIGHTBAR_S0S3);
		/* And transition on to the correct value */
		usleep(30 * SECOND);
		TEST_ASSERT(get_seq() == LIGHTBAR_S3);
	}

	return EC_SUCCESS;
}

static int test_stop_timeout(void)
{
	int i;

	for (i = 0; i < LIGHTBAR_NUM_SEQUENCES; i++) {
		/* Start in S0 */
		TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
		usleep(SECOND);
		/* Tell it to stop */
		TEST_ASSERT(set_seq(LIGHTBAR_STOP) == EC_RES_SUCCESS);
		usleep(SECOND);
		TEST_ASSERT(get_seq() == LIGHTBAR_STOP);
		/* Try to interrupt it */
		TEST_ASSERT(set_seq(i) == EC_RES_SUCCESS);
		usleep(SECOND);
		/* What happened? */
		if (i == LIGHTBAR_RUN ||
		    i == LIGHTBAR_S0S3 || i ==  LIGHTBAR_S3 ||
		    i ==  LIGHTBAR_S3S5 || i ==  LIGHTBAR_S5)
			/* RUN or shutdown sequences should stop it */
			TEST_ASSERT(get_seq() == LIGHTBAR_S0);
		else
			/* All other sequences should be ignored */
			TEST_ASSERT(get_seq() == LIGHTBAR_STOP);

		/* Let it RUN again for the next iteration */
		TEST_ASSERT(set_seq(LIGHTBAR_RUN) == EC_RES_SUCCESS);
		usleep(SECOND);
	}

	TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
	return EC_SUCCESS;
}

static int test_oneshots_timeout(void)
{
	/* These should revert to the previous state after running */
	enum lightbar_sequence seqs[] = {
		LIGHTBAR_RUN,
		LIGHTBAR_KONAMI,
		LIGHTBAR_TAP,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(seqs); i++) {
		TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
		usleep(SECOND);
		TEST_ASSERT(set_seq(seqs[i]) == EC_RES_SUCCESS);
		/* Assume the oneshot sequence takes at least a second (except
		 * for LIGHTBAR_RUN, which returns immediately) */
		if (seqs[i] != LIGHTBAR_RUN) {
			usleep(SECOND);
			TEST_ASSERT(get_seq() == seqs[i]);
		}
		usleep(30 * SECOND);
		TEST_ASSERT(get_seq() == LIGHTBAR_S0);
	}

	return EC_SUCCESS;
}

static int test_transition_states(void)
{
	/* S5S3 */
	TEST_ASSERT(set_seq(LIGHTBAR_S5S3) == EC_RES_SUCCESS);
	usleep(10 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S3);

	/* S3S0 */
	TEST_ASSERT(set_seq(LIGHTBAR_S3S0) == EC_RES_SUCCESS);
	usleep(10 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S0);

	/* S0S3 */
	TEST_ASSERT(set_seq(LIGHTBAR_S0S3) == EC_RES_SUCCESS);
	usleep(10 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S3);

	/* S3S5 */
	TEST_ASSERT(set_seq(LIGHTBAR_S3S5) == EC_RES_SUCCESS);
	usleep(10 * SECOND);
	TEST_ASSERT(get_seq() == LIGHTBAR_S5);

	return EC_SUCCESS;
}

static int test_stable_states(void)
{
	int i;

	/* Wait for the lightbar task to initialize */
	msleep(500);

	/* It should come up in S5 */
	TEST_ASSERT(get_seq() == LIGHTBAR_S5);

	/* It should stay there */
	for (i = 0; i < 30; i++) {
		usleep(SECOND);
		TEST_ASSERT(get_seq() == LIGHTBAR_S5);
	}

	/* S3 is sticky, too */
	TEST_ASSERT(set_seq(LIGHTBAR_S3) == EC_RES_SUCCESS);
	for (i = 0; i < 30; i++) {
		usleep(SECOND);
		TEST_ASSERT(get_seq() == LIGHTBAR_S3);
	}

	/* And S0 */
	TEST_ASSERT(set_seq(LIGHTBAR_S0) == EC_RES_SUCCESS);
	for (i = 0; i < 30; i++) {
		usleep(SECOND);
		TEST_ASSERT(get_seq() == LIGHTBAR_S0);
	}

	return EC_SUCCESS;
}

void run_test(void)
{
	RUN_TEST(test_stable_states);
	RUN_TEST(test_transition_states);
	RUN_TEST(test_oneshots_timeout);
	RUN_TEST(test_stop_timeout);
	RUN_TEST(test_oneshots_norm_msg);
	RUN_TEST(test_double_oneshots);
	test_print_result();
}
