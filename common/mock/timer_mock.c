/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/timer_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

static timestamp_t now;

void set_time(timestamp_t now_)
{
	now = now_;
}

timestamp_t get_time(void)
{
	return now;
};
