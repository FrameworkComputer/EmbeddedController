/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C VPD and CTVPD module.
 */
#include "common.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "usb_pd_test_util.h"
#include "vpd_api.h"

/*
 * Test State Hierarchy
 *   SM_TEST_A4 transitions to SM_TEST_B4
 *   SM_TEST_B4 transitions to SM_TEST_B5
 *   SM_TEST_B5 transitions to SM_TEST_B6
 *   SM_TEST_B6 transitions to SM_TEST_C
 *   SM_TEST_C  transitions to SM_TEST_A7
 *   SM_TEST_A7 transitions to SM_TEST_A6
 *   SM_TEST_A6 transitions to SM_TEST_A5
 *   SM_TEST_A5 transitions to SM_TEST_A4
 *
 * ---------------------------     ---------------------------
 * | SM_TEST_SUPER_A1        |     | SM_TEST_SUPER_B1        |
 * | ----------------------- |     | ----------------------- |
 * | | SM_TEST_SUPER_A2    | |     | | SM_TEST_SUPER_B2    | |
 * | | ------------------- | |     | | ------------------- | |
 * | | |SM_TEST_SUPER_A3 | | |     | | |SM_TEST_SUPER_B3 | | |
 * | | |                 | | |     | | |                 | | |
 * | | |  -------------  | | |     | | |  -------------  | | |
 * | | |  | SM_TEST_A4|------------------>| SM_TEST_B4|  | | |
 * | | |  -------------  | | |     | | |  -------------  | | |
 * | | |        ^        | | |     | | |--------|--------| | |
 * | | |        |        | | |     | |          |          | |
 * | | |  -------------- | | |     | |          \/         | |
 * | | |  | SM_TEST_A5 | | | |     | |    --------------   | |
 * | | |  -------------- | | |     | |    | SM_TEST_B5 |   | |
 * | | |--------^--------| | |     | |    --------------   | |
 * | |          |          | |     | |          |          | |
 * | |    --------------   | |     | -----------|----------- |
 * | |    | SM_TEST_A6 |   | |     |            \/           |
 * | |    --------------   | |     |      --------------     |
 * | |----------^----------| |     |      | SM_TEST_B6 |     |
 * |            |            |     |      --------------     |
 * |      --------------     |     |--------/----------------|
 * |      | SM_TEST_A7 |     |             /
 * |      --------------     |            /
 * |------------------^------|           /
 *                     \                /
 *                      \              \/
 *                        -------------
 *                        | SM_TEST_C |
 *                        -------------
 *
 * test_hierarchy_0: Tests a flat state machine without super states
 * test_hierarchy_1: Tests a hierarchical state machine with 1 super state
 * test_hierarchy_2: Tests a hierarchical state machine with 2 super states
 * test_hierarchy_3: Tests a hierarchical state machine with 3 super states
 *
 */

#define SEQUENCE_SIZE 55

enum state_id {
	ENTER_A1 = 1,
	RUN_A1,
	EXIT_A1,
	ENTER_A2,
	RUN_A2,
	EXIT_A2,
	ENTER_A3,
	RUN_A3,
	EXIT_A3,
	ENTER_A4,
	RUN_A4,
	EXIT_A4,
	ENTER_A5,
	RUN_A5,
	EXIT_A5,
	ENTER_A6,
	RUN_A6,
	EXIT_A6,
	ENTER_A7,
	RUN_A7,
	EXIT_A7,
	ENTER_B1,
	RUN_B1,
	EXIT_B1,
	ENTER_B2,
	RUN_B2,
	EXIT_B2,
	ENTER_B3,
	RUN_B3,
	EXIT_B3,
	ENTER_B4,
	RUN_B4,
	EXIT_B4,
	ENTER_B5,
	RUN_B5,
	EXIT_B5,
	ENTER_B6,
	RUN_B6,
	EXIT_B6,
	ENTER_C,
	RUN_C,
	EXIT_C,
};

#define PORT0   0

struct sm_ {
	/* struct sm_obj must be first */
	struct sm_ctx ctx;
	int sv_tmp;
	int idx;
	int seq[SEQUENCE_SIZE];
} sm[1];

enum state {
	SM_TEST_SUPER_A1,
	SM_TEST_SUPER_A2,
	SM_TEST_SUPER_A3,
	SM_TEST_SUPER_B1,
	SM_TEST_SUPER_B2,
	SM_TEST_SUPER_B3,
	SM_TEST_A4,
	SM_TEST_A5,
	SM_TEST_A6,
	SM_TEST_A7,
	SM_TEST_B4,
	SM_TEST_B5,
	SM_TEST_B6,
	SM_TEST_C,
};
static const struct usb_state states[];

static struct control {
	usb_state_ptr a3_entry_to;
	usb_state_ptr b3_run_to;
	usb_state_ptr b6_entry_to;
	usb_state_ptr c_entry_to;
	usb_state_ptr c_exit_to;
} test_control;

static void set_state_sm(const int port, const enum state new_state)
{
	set_state(port, &sm[port].ctx, &states[new_state]);
}

static void sm_test_super_A1_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A1;
}

static void sm_test_super_A1_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A1;
}

static void sm_test_super_A1_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A1;
}

static void sm_test_super_B1_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B1;
}

static void sm_test_super_B1_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B1;
}

static void sm_test_super_B1_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B1;
}

static void sm_test_super_A2_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A2;
}

static void sm_test_super_A2_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A2;
}

static void sm_test_super_A2_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A2;
}


static void sm_test_super_B2_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B2;
}

static void sm_test_super_B2_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B2;
}

static void sm_test_super_B2_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B2;
}

static void sm_test_super_A3_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A3;
	if (test_control.a3_entry_to)
		set_state(port, &sm[port].ctx, test_control.a3_entry_to);
}

static void sm_test_super_A3_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A3;
}

static void sm_test_super_A3_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A3;
}

static void sm_test_super_B3_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B3;
}

static void sm_test_super_B3_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B3;
	if (test_control.b3_run_to)
		set_state(port, &sm[port].ctx, test_control.b3_run_to);
}

static void sm_test_super_B3_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B3;
}

static void sm_test_A4_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A4;
}

static void sm_test_A4_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A4;
	} else {
		set_state_sm(port, SM_TEST_B4);
	}
}

static void sm_test_A4_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A4;
}


static void sm_test_A5_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A5;
}

static void sm_test_A5_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A5;
	} else {
		set_state_sm(port, SM_TEST_A4);
	}
}

static void sm_test_A5_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A5;
}


static void sm_test_A6_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A6;
}

static void sm_test_A6_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A6;
	} else {
		set_state_sm(port, SM_TEST_A5);
	}
}

static void sm_test_A6_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A6;
}

static void sm_test_A7_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A7;
}

static void sm_test_A7_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A7;
	} else {
		set_state_sm(port, SM_TEST_A6);
	}
}

static void sm_test_A7_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A7;
}

static void sm_test_B4_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B4;
}

static void sm_test_B4_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].seq[sm[port].idx++] = RUN_B4;
		sm[port].sv_tmp = 1;
	} else {
		set_state_sm(port, SM_TEST_B5);
	}
}

static void sm_test_B4_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B4;
}


static void sm_test_B5_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B5;
}

static void sm_test_B5_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_B5;
	} else {
		set_state_sm(port, SM_TEST_B6);
	}
}

static void sm_test_B5_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B5;
}


static void sm_test_B6_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B6;
	if (test_control.b6_entry_to)
		set_state(port, &sm[port].ctx, test_control.b6_entry_to);
}

static void sm_test_B6_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_B6;
	} else {
		set_state_sm(port, SM_TEST_C);
	}
}

static void sm_test_B6_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B6;
}

static void sm_test_C_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_C;
	if (test_control.c_entry_to)
		set_state(port, &sm[port].ctx, test_control.c_entry_to);
}

static void sm_test_C_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].seq[sm[port].idx++] = RUN_C;
		sm[port].sv_tmp = 1;
	} else {
		set_state_sm(port, SM_TEST_A7);
	}
}

static void sm_test_C_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_C;
	if (test_control.c_exit_to)
		set_state(port, &sm[port].ctx, test_control.c_exit_to);
}

static void run_sm(void)
{
	task_wake(TASK_ID_TEST);
	task_wait_event(5 * MSEC);
}

test_static int test_hierarchy_0(void)
{
	int port = PORT0;
	int i = 0;

	set_state_sm(port, SM_TEST_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0, "%d");

	return EC_SUCCESS;
}

test_static int test_hierarchy_1(void)
{
	int port = PORT0;
	int i = 0;

	set_state_sm(port, SM_TEST_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	for (i = 33; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0, "%d");

	return EC_SUCCESS;
}

test_static int test_hierarchy_2(void)
{

	int port = PORT0;
	int i = 0;

	set_state_sm(port, SM_TEST_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0, "%d");

	return EC_SUCCESS;
}

test_static int test_hierarchy_3(void)
{

	int port = PORT0;
	int i = 0;

	set_state_sm(port, SM_TEST_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1, "%d"); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4, "%d"); ++i;

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0, "%d");

	return EC_SUCCESS;
}

test_static int test_set_state_from_parents(void)
{
	int port = PORT0;
	int i = 0;

	/* Start state machine */
	test_control.a3_entry_to = &states[SM_TEST_B4];
	run_sm();
	set_state_sm(port, SM_TEST_A4);
	TEST_EQ(sm[port].seq[i], ENTER_A1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3, "%d"); ++i;
	/* Does not enter or exit A4 */
	TEST_EQ(sm[port].seq[i], EXIT_A3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4, "%d"); ++i;
	/* Ensure we didn't go further than above statements */
	TEST_EQ(sm[port].seq[i], 0, "%d");

	test_control.b3_run_to = &states[SM_TEST_B5];
	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3, "%d"); ++i;
	/* Does not run b2 or b1 */
	TEST_EQ(sm[port].seq[i], EXIT_B4, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5, "%d"); ++i;
	/* Ensure we didn't go further than above statements */
	TEST_EQ(sm[port].seq[i], 0, "%d");

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1, "%d"); ++i;
	/* Ensure we didn't go further than above statements */
	TEST_EQ(sm[port].seq[i], 0, "%d");

	/*
	 * Ensure that multiple chains of parent entry works. Also ensure
	 * that set states in exit are ignored.
	 */
	test_control.b6_entry_to = &states[SM_TEST_C];
	test_control.c_entry_to = &states[SM_TEST_A7];
	test_control.c_exit_to = &states[SM_TEST_A4];
	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B2, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B6, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_C, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A1, "%d"); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7, "%d"); ++i;
	/* Ensure we didn't go further than above statements */
	TEST_EQ(sm[port].seq[i], 0, "%d");

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0, "%d");

	return EC_SUCCESS;
}

#ifdef TEST_USB_SM_FRAMEWORK_H3
#define TEST_AT_LEAST_3
#endif

#if defined(TEST_AT_LEAST_3) || defined(TEST_USB_SM_FRAMEWORK_H2)
#define TEST_AT_LEAST_2
#endif

#if defined(TEST_AT_LEAST_2) || defined(TEST_USB_SM_FRAMEWORK_H1)
#define TEST_AT_LEAST_1
#endif

static const struct usb_state states[] = {
	[SM_TEST_SUPER_A1] = {
		.entry  = sm_test_super_A1_entry,
		.run    = sm_test_super_A1_run,
		.exit   = sm_test_super_A1_exit,
	},
	[SM_TEST_SUPER_A2] = {
		.entry  = sm_test_super_A2_entry,
		.run    = sm_test_super_A2_run,
		.exit   = sm_test_super_A2_exit,
#ifdef TEST_AT_LEAST_3
		.parent = &states[SM_TEST_SUPER_A1],
#endif
	},
	[SM_TEST_SUPER_A3] = {
		.entry  = sm_test_super_A3_entry,
		.run    = sm_test_super_A3_run,
		.exit   = sm_test_super_A3_exit,
#ifdef TEST_AT_LEAST_2
		.parent = &states[SM_TEST_SUPER_A2],
#endif
	},
	[SM_TEST_SUPER_B1] = {
		.entry  = sm_test_super_B1_entry,
		.run    = sm_test_super_B1_run,
		.exit   = sm_test_super_B1_exit,
	},
	[SM_TEST_SUPER_B2] = {
		.entry  = sm_test_super_B2_entry,
		.run    = sm_test_super_B2_run,
		.exit   = sm_test_super_B2_exit,
#ifdef TEST_AT_LEAST_3
		.parent = &states[SM_TEST_SUPER_B1],
#endif
	},
	[SM_TEST_SUPER_B3] = {
		.entry  = sm_test_super_B3_entry,
		.run    = sm_test_super_B3_run,
		.exit   = sm_test_super_B3_exit,
#ifdef TEST_AT_LEAST_2
		.parent = &states[SM_TEST_SUPER_B2],
#endif
	},
	[SM_TEST_A4] = {
		.entry  = sm_test_A4_entry,
		.run    = sm_test_A4_run,
		.exit   = sm_test_A4_exit,
#ifdef TEST_AT_LEAST_1
		.parent = &states[SM_TEST_SUPER_A3],
#endif
	},
	[SM_TEST_A5] = {
		.entry  = sm_test_A5_entry,
		.run    = sm_test_A5_run,
		.exit   = sm_test_A5_exit,
#ifdef TEST_AT_LEAST_1
		.parent = &states[SM_TEST_SUPER_A3],
#endif
	},
	[SM_TEST_A6] = {
		.entry  = sm_test_A6_entry,
		.run    = sm_test_A6_run,
		.exit   = sm_test_A6_exit,
#ifdef TEST_AT_LEAST_2
		.parent = &states[SM_TEST_SUPER_A2],
#endif
	},
	[SM_TEST_A7] = {
		.entry  = sm_test_A7_entry,
		.run    = sm_test_A7_run,
		.exit   = sm_test_A7_exit,
#ifdef TEST_AT_LEAST_3
		.parent = &states[SM_TEST_SUPER_A1],
#endif
	},
	[SM_TEST_B4] = {
		.entry  = sm_test_B4_entry,
		.run    = sm_test_B4_run,
		.exit   = sm_test_B4_exit,
#ifdef TEST_AT_LEAST_1
		.parent = &states[SM_TEST_SUPER_B3],
#endif
	},
	[SM_TEST_B5] = {
		.entry  = sm_test_B5_entry,
		.run    = sm_test_B5_run,
		.exit   = sm_test_B5_exit,
#ifdef TEST_AT_LEAST_2
		.parent = &states[SM_TEST_SUPER_B2],
#endif
	},
	[SM_TEST_B6] = {
		.entry  = sm_test_B6_entry,
		.run    = sm_test_B6_run,
		.exit   = sm_test_B6_exit,
#ifdef TEST_AT_LEAST_3
		.parent = &states[SM_TEST_SUPER_B1],
#endif
	},
	[SM_TEST_C] = {
		.entry  = sm_test_C_entry,
		.run    = sm_test_C_run,
		.exit   = sm_test_C_exit,
	},
};

/* Run before each RUN_TEST line */
void before_test(void)
{
	/* Rest test variables */
	memset(&sm[PORT0], 0, sizeof(struct sm_));
	memset(&test_control, 0, sizeof(struct control));
}

int test_task(void *u)
{
	int port = PORT0;

	while (1) {
		/* wait for next event/packet or timeout expiration */
		task_wait_event(-1);
		/* run state machine */
		run_state(port, &sm[port].ctx);
	}

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();
#if defined(TEST_USB_SM_FRAMEWORK_H3)
	RUN_TEST(test_hierarchy_3);
	RUN_TEST(test_set_state_from_parents);
#elif defined(TEST_USB_SM_FRAMEWORK_H2)
	RUN_TEST(test_hierarchy_2);
#elif defined(TEST_USB_SM_FRAMEWORK_H1)
	RUN_TEST(test_hierarchy_1);
#else
	RUN_TEST(test_hierarchy_0);
#endif
	test_print_result();
}
