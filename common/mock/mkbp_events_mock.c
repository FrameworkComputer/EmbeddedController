/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Mock event handling for MKBP keyboard protocol
 */

#include <stdint.h>

#include "common.h"
#include "mock/mkbp_events_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_ctrl_mkbp_events mock_ctrl_mkbp_events =
	MOCK_CTRL_DEFAULT_MKBP_EVENTS;

int mkbp_send_event(uint8_t event_type)
{
	return mock_ctrl_mkbp_events.mkbp_send_event_return;
}
