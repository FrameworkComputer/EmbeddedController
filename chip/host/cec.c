/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* CEC module for Chrome EC emulator */

#include "cec.h"

test_mockable void cec_tmr_cap_start(enum cec_cap_edge edge, int timeout)
{
	/* Do nothing */
}

test_mockable void cec_tmr_cap_stop(void)
{
	/* Do nothing */
}

test_mockable int cec_tmr_cap_get(void)
{
	return 0;
}

test_mockable void cec_trigger_send(void)
{
	/* Do nothing */
}

test_mockable void cec_enable_timer(void)
{
	/* Do nothing */
}

test_mockable void cec_disable_timer(void)
{
	/* Do nothing */
}

test_mockable void cec_init_timer(void)
{
	/* Do nothing */
}
