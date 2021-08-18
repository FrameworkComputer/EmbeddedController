/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock USB PE state machine */

#include "common.h"
#include "console.h"
#include "usb_pd.h"
#include "usb_pe_sm.h"
#include "mock/usb_pe_sm_mock.h"
#include "memory.h"
#include "usb_pd_tcpm.h"

#ifndef CONFIG_COMMON_RUNTIME
#define cprints(format, args...)
#endif

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_pe_port_t mock_pe_port[CONFIG_USB_PD_PORT_MAX_COUNT];


/**
 * Resets all mock PE ports to initial values
 */
void mock_pe_port_reset(void)
{
	int port;

	for (port = 0 ; port < CONFIG_USB_PD_PORT_MAX_COUNT ; ++port) {
		mock_pe_port[port].mock_pe_error = -1;
		/* These mock variable only get set to 1 by various functions,
		 * so initialize them to 0. Tests can verify they are still 0
		 * if that's part of the pass criteria.
		 */
		mock_pe_port[port].mock_pe_message_received = 0;
		mock_pe_port[port].mock_pe_message_sent = 0;
		mock_pe_port[port].mock_pe_message_discarded = 0;
		mock_pe_port[port].mock_got_soft_reset = 0;
		mock_pe_port[port].mock_pe_got_hard_reset = 0;
		mock_pe_port[port].mock_pe_hard_reset_sent = 0;
	}
}

void pe_report_error(int port, enum pe_error e, enum tcpm_sop_type type)
{
	mock_pe_port[port].mock_pe_error = e;
	mock_pe_port[port].sop = type;
}

void pe_report_discard(int port)
{
	mock_pe_port[port].mock_pe_message_discarded = 1;
}

void pe_got_hard_reset(int port)
{
	mock_pe_port[port].mock_pe_got_hard_reset = 1;
}

void pe_message_received(int port)
{
	mock_pe_port[port].mock_pe_message_received = 1;
}

void pe_message_sent(int port)
{
	mock_pe_port[port].mock_pe_message_sent = 1;
}

void pe_hard_reset_sent(int port)
{
	mock_pe_port[port].mock_pe_hard_reset_sent = 1;
}

void pe_got_soft_reset(int port)
{
	mock_pe_port[port].mock_got_soft_reset = 1;
}

bool pe_in_frs_mode(int port)
{
	return false;
}

bool pe_in_local_ams(int port)
{
	/* We will probably want to change this in the future */
	return false;
}

const uint32_t * const pd_get_src_caps(int port)
{
	return NULL;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
}

void pd_request_power_swap(int port)
{}

int pd_get_rev(int port, enum tcpm_sop_type type)
{
	return IS_ENABLED(CONFIG_USB_PD_REV30) ? PD_REV30 : PD_REV20;
}

void pe_invalidate_explicit_contract(int port)
{
}
