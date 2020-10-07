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

#ifdef CONFIG_USB_TYPEC_SM
extern const struct test_sm_data test_tc_sm_data[];
extern const int test_tc_sm_data_size;
#else
const struct test_sm_data test_tc_sm_data[] = {};
const int test_tc_sm_data_size;
#endif

#ifdef CONFIG_USB_PRL_SM
extern const struct test_sm_data test_prl_sm_data[];
extern const int test_prl_sm_data_size;
#else
const struct test_sm_data test_prl_sm_data[] = {};
const int test_prl_sm_data_size;
#endif

#ifdef CONFIG_USB_PE_SM
extern const struct test_sm_data test_pe_sm_data[];
extern const int test_pe_sm_data_size;
#else
const struct test_sm_data test_pe_sm_data[] = {};
const int test_pe_sm_data_size;
#endif

test_static int test_no_parent_cycles(const struct test_sm_data * const sm_data)
{
	int i;

	for (i = 0; i < sm_data->size; ++i) {
		int depth = 0;
		usb_state_ptr current = &sm_data->base[i];

		while (current != NULL && ++depth <= sm_data->size)
			current = current->parent;

		if (depth > sm_data->size)
			break;
	}

	/* Ensure all states end, otherwise the ith state has a cycle. */
	TEST_EQ(i, sm_data->size, "%d");

	return EC_SUCCESS;
}

int test_tc_no_parent_cycles(void)
{
	int i;

	for (i = 0; i < test_tc_sm_data_size; ++i) {
		const int rv = test_no_parent_cycles(&test_tc_sm_data[i]);

		if (rv) {
			ccprintf("TC State machine %d has a cycle!\n", i);
			TEST_ASSERT(0);
		}
	}

	return EC_SUCCESS;
}

int test_prl_no_parent_cycles(void)
{
	int i;

	for (i = 0; i < test_prl_sm_data_size; ++i) {
		const int rv = test_no_parent_cycles(&test_prl_sm_data[i]);

		if (rv) {
			ccprintf("PRL State machine %d has a cycle!\n", i);
			TEST_ASSERT(0);
		}
	}

	return EC_SUCCESS;
}

int test_pe_no_parent_cycles(void)
{
	int i;

	for (i = 0; i < test_pe_sm_data_size; ++i) {
		const int rv = test_no_parent_cycles(&test_pe_sm_data[i]);

		if (rv) {
			ccprintf("PE State machine %d has a cycle!\n", i);
			TEST_ASSERT(0);
		}
	}

	return EC_SUCCESS;
}

static volatile int state_printed;

/* Override the implement version of print */
__override void print_current_state(const int port)
{
	state_printed = 1;
}

static int test_all_states_named(const struct test_sm_data * const sm_data)
{
	int i;

	for (i = 0; i < sm_data->size; ++i) {
		usb_state_ptr current = &sm_data->base[i];

		state_printed = 0;

		if (current->entry)
			current->entry(0);

		if (state_printed) {
			if (i >= sm_data->names_size ||
			    sm_data->names[i] == NULL) {
				ccprintf("State %d does not have a name!\n", i);
				TEST_ASSERT(0);
			}
		}
	}

	return EC_SUCCESS;
}

int test_tc_all_states_named(void)
{
	int i;

	for (i = 0; i < test_tc_sm_data_size; ++i) {
		const int rv = test_all_states_named(&test_tc_sm_data[i]);

		if (rv) {
			ccprintf("TC State machine %d has empty name!\n", i);
			TEST_ASSERT(0);
		}
	}

	return EC_SUCCESS;
}

int test_prl_all_states_named(void)
{
	int i;

	for (i = 0; i < test_prl_sm_data_size; ++i) {
		const int rv = test_all_states_named(&test_prl_sm_data[i]);

		if (rv) {
			ccprintf("PRL State machine %d has empty name!\n", i);
			TEST_ASSERT(0);
		}
	}

	return EC_SUCCESS;
}

int test_pe_all_states_named(void)
{
	int i;

	for (i = 0; i < test_pe_sm_data_size; ++i) {
		const int rv = test_all_states_named(&test_pe_sm_data[i]);

		if (rv) {
			ccprintf("PE State machine %d has empty name!\n", i);
			TEST_ASSERT(0);
		}
	}

	return EC_SUCCESS;
}

