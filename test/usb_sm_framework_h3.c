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
#define TSM_OBJ(port)   (SM_OBJ(sm[port]))

struct sm_ {
	/* struct sm_obj must be first */
	struct sm_obj obj;
	int sv_tmp;
	int idx;
	int seq[55];
} sm[1];

#if defined(TEST_USB_SM_FRAMEWORK_H3)
DECLARE_STATE(sm, test_super_A1, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_super_B1, WITH_RUN, WITH_EXIT);
#endif

#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
DECLARE_STATE(sm, test_super_A2, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_super_B2, WITH_RUN, WITH_EXIT);
#endif

#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2) || \
	defined(TEST_USB_SM_FRAMEWORK_H1)
DECLARE_STATE(sm, test_super_A3, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_super_B3, WITH_RUN, WITH_EXIT);
#endif

DECLARE_STATE(sm, test_A4, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_A5, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_A6, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_A7, WITH_RUN, WITH_EXIT);

DECLARE_STATE(sm, test_B4, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_B5, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_B6, WITH_RUN, WITH_EXIT);
DECLARE_STATE(sm, test_C, WITH_RUN, WITH_EXIT);

static void clear_seq(int port)
{
	int i;

	sm[port].idx = 0;

	for (i = 0; i < 8; i++)
		sm[port].seq[i] = 0;
}

#if defined(TEST_USB_SM_FRAMEWORK_H3)
static int sm_test_super_A1(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_super_A1_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int sm_test_super_A1_entry(int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A1;
	return 0;
}

static int sm_test_super_A1_run(int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A1;
	return 0;
}

static int sm_test_super_A1_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A1;
	return 0;
}

static int sm_test_super_B1(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_super_B1_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int sm_test_super_B1_entry(int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B1;
	return 0;
}

static int sm_test_super_B1_run(int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B1;
	return 0;
}

static int sm_test_super_B1_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B1;
	return 0;
}
#endif

#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
static int sm_test_super_A2(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_super_A2_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3)
	return SM_SUPER(ret, sig, sm_test_super_A1);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_super_A2_entry(int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A2;
	return 0;
}

static int sm_test_super_A2_run(int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A2;
	return SM_RUN_SUPER;
}

static int sm_test_super_A2_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A2;
	return 0;
}

static int sm_test_super_B2(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_super_B2_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3)
	return SM_SUPER(ret, sig, sm_test_super_B1);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_super_B2_entry(int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B2;
	return 0;
}

static int sm_test_super_B2_run(int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B2;
	return SM_RUN_SUPER;
}

static int sm_test_super_B2_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B2;
	return 0;
}
#endif

#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2) || \
		defined(TEST_USB_SM_FRAMEWORK_H1)
static int sm_test_super_A3(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_super_A3_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
	return SM_SUPER(ret, sig, sm_test_super_A2);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_super_A3_entry(int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A3;
	return 0;
}

static int sm_test_super_A3_run(int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A3;
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
	return SM_RUN_SUPER;
#else
	return 0;
#endif
}

static int sm_test_super_A3_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A3;
	return 0;
}

static int sm_test_super_B3(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_super_B3_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
	return SM_SUPER(ret, sig, sm_test_super_B2);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_super_B3_entry(int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B3;
	return 0;
}

static int sm_test_super_B3_run(int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B3;
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
	return SM_RUN_SUPER;
#else
	return 0;
#endif
}

static int sm_test_super_B3_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B3;
	return 0;
}
#endif


static int sm_test_A4(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_A4_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2) || \
		defined(TEST_USB_SM_FRAMEWORK_H1)
	return SM_SUPER(ret, sig, sm_test_super_A3);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_A4_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A4;
	return 0;
}

static int sm_test_A4_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A4;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_B4);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_A4_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A4;
	return 0;
}

static int sm_test_A5(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_A5_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2) || \
		defined(TEST_USB_SM_FRAMEWORK_H1)
	return SM_SUPER(ret, sig, sm_test_super_A3);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_A5_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A5;
	return 0;
}

static int sm_test_A5_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A5;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_A4);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_A5_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A5;
	return 0;
}

static int sm_test_A6(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_A6_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
	return SM_SUPER(ret, sig, sm_test_super_A2);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_A6_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A6;
	return 0;
}

static int sm_test_A6_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A6;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_A5);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_A6_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A6;
	return 0;
}

static int sm_test_A7(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_A7_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3)
	return SM_SUPER(ret, sig, sm_test_super_A1);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_A7_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A7;
	return 0;
}

static int sm_test_A7_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A7;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_A6);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_A7_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A7;
	return 0;
}

static int sm_test_B4(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_B4_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2) || \
		defined(TEST_USB_SM_FRAMEWORK_H1)
	return SM_SUPER(ret, sig, sm_test_super_B3);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_B4_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B4;
	return 0;
}

static int sm_test_B4_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].seq[sm[port].idx++] = RUN_B4;
		sm[port].sv_tmp = 1;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_B5);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_B4_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B4;
	return 0;
}

static int sm_test_B5(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_B5_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3) || defined(TEST_USB_SM_FRAMEWORK_H2)
	return SM_SUPER(ret, sig, sm_test_super_B2);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_B5_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B5;
	return 0;
}

static int sm_test_B5_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_B5;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_B6);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_B5_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B5;
	return 0;
}

static int sm_test_B6(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_B6_sig[sig])(port);
#if defined(TEST_USB_SM_FRAMEWORK_H3)
	return SM_SUPER(ret, sig, sm_test_super_B1);
#else
	return SM_SUPER(ret, sig, 0);
#endif
}

static int sm_test_B6_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B6;
	return 0;
}

static int sm_test_B6_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_B6;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_C);
		return 0;
	}

	return SM_RUN_SUPER;
}

static int sm_test_B6_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B6;
	return 0;
}

static int sm_test_C(int port, enum sm_signal sig)
{
	int ret;

	ret = (*sm_test_C_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int sm_test_C_entry(int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_C;
	return 0;
}

static int sm_test_C_run(int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].seq[sm[port].idx++] = RUN_C;
		sm[port].sv_tmp = 1;
	} else {
		sm_set_state(port, TSM_OBJ(port), sm_test_A7);
	}

	return 0;
}

static int sm_test_C_exit(int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_C;
	return 0;
}

static void run_sm(void)
{
	task_wake(TASK_ID_TEST);
	task_wait_event(5 * MSEC);
}

#if defined(TEST_USB_SM_FRAMEWORK_H0)
static int test_hierarchy_0(void)
{
	int port = PORT0;
	int i = 0;

	clear_seq(port);
	sm_init_state(port, TSM_OBJ(port), sm_test_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0);

	return EC_SUCCESS;
}
#endif


#if defined(TEST_USB_SM_FRAMEWORK_H1)
static int test_hierarchy_1(void)
{
	int port = PORT0;
	int i = 0;

	clear_seq(port);
	sm_init_state(port, TSM_OBJ(port), sm_test_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	for (i = 33; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0);

	return EC_SUCCESS;
}
#endif


#if defined(TEST_USB_SM_FRAMEWORK_H2)
static int test_hierarchy_2(void)
{

	int port = PORT0;
	int i = 0;

	clear_seq(port);
	sm_init_state(port, TSM_OBJ(port), sm_test_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A3); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0);

	return EC_SUCCESS;
}
#endif

#if defined(TEST_USB_SM_FRAMEWORK_H3)
static int test_hierarchy_3(void)
{

	int port = PORT0;
	int i = 0;

	clear_seq(port);
	sm_init_state(port, TSM_OBJ(port), sm_test_A4);

	run_sm();
	TEST_EQ(sm[port].seq[i], ENTER_A1); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A4); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A4); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A3); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A2); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_A1); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B1); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B4); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B4); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B3); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B4); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B5); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B2); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B5); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_B6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_B6); ++i;
	TEST_EQ(sm[port].seq[i], RUN_B1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_B6); ++i;
	TEST_EQ(sm[port].seq[i], EXIT_B1); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_C); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_C); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A1); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A7); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A7); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A7); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A2); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A6); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A6); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A6); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A3); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A5); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], RUN_A5); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A3); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A2); ++i;
	TEST_EQ(sm[port].seq[i], RUN_A1); ++i;

	run_sm();
	TEST_EQ(sm[port].seq[i], EXIT_A5); ++i;
	TEST_EQ(sm[port].seq[i], ENTER_A4); ++i;

	for (; i < SEQUENCE_SIZE; i++)
		TEST_EQ(sm[port].seq[i], 0);

	return EC_SUCCESS;
}
#endif

int test_task(void *u)
{
	int port = PORT0;

	while (1) {
		/* wait for next event/packet or timeout expiration */
		task_wait_event(-1);
		/* run state machine */
		sm_run_state_machine(port, TSM_OBJ(port), SM_RUN_SIG);
	}

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();
#if defined(TEST_USB_SM_FRAMEWORK_H3)
	RUN_TEST(test_hierarchy_3);
#elif defined(TEST_USB_SM_FRAMEWORK_H2)
	RUN_TEST(test_hierarchy_2);
#elif defined(TEST_USB_SM_FRAMEWORK_H1)
	RUN_TEST(test_hierarchy_1);
#else
	RUN_TEST(test_hierarchy_0);
#endif
	test_print_result();
}
