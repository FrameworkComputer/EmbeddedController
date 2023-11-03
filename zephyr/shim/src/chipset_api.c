/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset interface APIs */

#include "ap_power/ap_power_interface.h"
#include "charge_state.h"
#include "chipset_state_check.h"
#include "common.h"
#include "system.h"

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
void chipset_throttle_cpu(int throttle)
{
}

bool board_ap_power_is_startup_ok(void)
{
	/*
	 * Try multiple times with some delay to allow chargers to become ready
	 * if needed. 40 tries with 100ms delay is arbitrary, but follows all
	 * existing systems.
	 */
	for (int tries = 0; tries < 40; tries++) {
		/*
		 * TODO(b/260909787) this logic should be handled by a single
		 * function that works in all configurations. system_can_boot_ap
		 * is a subset of charge_prevent_power_on, but works in all
		 * configurations.
		 */
		bool power_ok = IS_ENABLED(CONFIG_CHARGER) &&
						IS_ENABLED(CONFIG_BATTERY) ?
					!charge_prevent_power_on(false) :
					system_can_boot_ap();
		if (power_ok)
			return true;

		msleep(100);
	}
	return false;
}
