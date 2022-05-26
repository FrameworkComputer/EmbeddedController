/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset interface APIs */

#include "common.h"

#include "ap_power/ap_power_interface.h"
#include "chipset_state_check.h"

int chipset_in_state(int state_mask)
{
	return ap_power_in_state(state_mask);
}

int chipset_in_or_transitioning_to_state(int state_mask)
{
	return ap_power_in_or_transitioning_to_state(state_mask);
}

void chipset_exit_hard_off(void)
{
	ap_power_exit_hardoff();
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	ap_power_force_shutdown((enum ap_power_shutdown_reason)reason);
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	ap_power_reset((enum ap_power_shutdown_reason)reason);
}

/* TODO: b/214509787
 * To be added later when this functionality is implemented in ap_pwrseq.
 */
void chipset_throttle_cpu(int throttle) { }

void init_reset_log(void)
{
	ap_power_init_reset_log();
}
