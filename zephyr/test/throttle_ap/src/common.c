/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>

#include <chipset.h>
#include <console.h>
#include <throttle_ap.h>

ZTEST_SUITE(throttle_ap, NULL, NULL, NULL, NULL, NULL);

/*
 * The prochot interrupt handler will return early if the chipset is
 * off or suspended.
 */
int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ON;
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
}

int extpower_is_present(void)
{
	return 1;
}
