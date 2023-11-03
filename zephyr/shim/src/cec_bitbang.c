/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/cec/bitbang.h"

/* Do-nothing implementations which can be overridden for testing. */

test_mockable void cec_tmr_cap_start(int port, enum cec_cap_edge edge,
				     int timeout)
{
}

void cec_tmr_cap_stop(int port)
{
}

test_mockable int cec_tmr_cap_get(int port)
{
	return 0;
}

test_mockable void cec_debounce_enable(int port)
{
}

test_mockable void cec_debounce_disable(int port)
{
}

test_mockable void cec_trigger_send(int port)
{
}

void cec_enable_timer(int port)
{
}

void cec_disable_timer(int port)
{
}

void cec_init_timer(int port)
{
}
