/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

/* LCOV_EXCL_START empty function stub required for build */
void board_touchpad_reset(void)
{
}
/* LCOV_EXCL_END */

static void ec_ec_comm_init(void)
{
	/* b/300403990: put these here before driver ready */
	uint8_t lcr_cache = *(volatile uint8_t *)0xf02803;

	*(volatile uint8_t *)0xf02803 |= 0x80; /* access divisor latches */
	*(volatile uint8_t *)0xf02800 = 0x01; /* set divisor = 0x8001 */
	*(volatile uint8_t *)0xf02801 = 0x80;
	*(volatile uint8_t *)0xf02803 = lcr_cache;
	*(volatile uint8_t *)0xf02808 = 2; /* high speed select */
}
DECLARE_HOOK(HOOK_INIT, ec_ec_comm_init, HOOK_PRIO_DEFAULT);
