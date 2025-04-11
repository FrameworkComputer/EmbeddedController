/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/cec/bitbang.h"
#include "drivers/cec_counter.h"

test_mockable void cec_tmr_cap_start(int port, enum cec_cap_edge edge,
				     int timeout)
{
	cros_cec_bitbang_tmr_cap_start(port, edge, timeout);
}

void cec_tmr_cap_stop(int port)
{
	cros_cec_bitbang_tmr_cap_stop(port);
}

test_mockable int cec_tmr_cap_get(int port)
{
	return cros_cec_bitbang_tmr_cap_get(port);
}

test_mockable void cec_debounce_enable(int port)
{
	cros_cec_bitbang_debounce_enable(port);
}

test_mockable void cec_debounce_disable(int port)
{
	cros_cec_bitbang_debounce_disable(port);
}

test_mockable void cec_trigger_send(int port)
{
	cros_cec_bitbang_trigger_send(port);
}

void cec_enable_timer(int port)
{
	cros_cec_bitbang_enable_timer(port);
}

void cec_disable_timer(int port)
{
	cros_cec_bitbang_disable_timer(port);
}

void cec_init_timer(int port)
{
	/* Do nothing. */
}
