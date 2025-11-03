/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/clock_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

static int fast_cpu_state;

void clock_enable_module(enum module_id module, int enable)
{
	if (module == MODULE_FAST_CPU) {
		fast_cpu_state = enable;
	}
}

int get_mock_fast_cpu_status(void)
{
	return fast_cpu_state;
}
