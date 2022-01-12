/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ap_pwrseq_chipset.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

bool ap_pwrseq_chipset_in_state(
		enum ap_pwrseq_chipset_state_mask state_mask)
{
	int need_mask = 0;

	switch (pwr_sm_get_state()) {
	case SYS_POWER_STATE_G3:
		need_mask = AP_PWRSEQ_CHIPSET_STATE_HARD_OFF;
		break;
	case SYS_POWER_STATE_G3S5:
	case SYS_POWER_STATE_S5G3:
		/*
		 * In between hard and soft off states.  Match only if caller
		 * will accept both.
		 */
		need_mask = AP_PWRSEQ_CHIPSET_STATE_HARD_OFF |
			AP_PWRSEQ_CHIPSET_STATE_SOFT_OFF;
		break;
	case SYS_POWER_STATE_S5:
		need_mask = AP_PWRSEQ_CHIPSET_STATE_SOFT_OFF;
		break;
	case SYS_POWER_STATE_S5S4:
	case SYS_POWER_STATE_S4S5:
		need_mask = AP_PWRSEQ_CHIPSET_STATE_SOFT_OFF |
			AP_PWRSEQ_CHIPSET_STATE_SUSPEND;
		break;
	case SYS_POWER_STATE_S4:
	case SYS_POWER_STATE_S4S3:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S3:
		need_mask = AP_PWRSEQ_CHIPSET_STATE_SUSPEND;
		break;
	case SYS_POWER_STATE_S3S0:
	case SYS_POWER_STATE_S0S3:
		need_mask = AP_PWRSEQ_CHIPSET_STATE_SUSPEND |
			AP_PWRSEQ_CHIPSET_STATE_ON;
		break;
	case SYS_POWER_STATE_S0:
		need_mask = AP_PWRSEQ_CHIPSET_STATE_ON;
		break;
	/* TODO: b/203446865 S0ix */
	}
	/* Return non-zero if all needed bits are present */
	return (state_mask & need_mask) == need_mask;
}

bool ap_pwrseq_chipset_in_or_transitioning_to_state(
		enum ap_pwrseq_chipset_state_mask state_mask)
{
	switch (pwr_sm_get_state()) {
	case SYS_POWER_STATE_G3:
	case SYS_POWER_STATE_S5G3:
		return state_mask & AP_PWRSEQ_CHIPSET_STATE_HARD_OFF;
	case SYS_POWER_STATE_S5:
	case SYS_POWER_STATE_G3S5:
	case SYS_POWER_STATE_S4S5:
		return state_mask & AP_PWRSEQ_CHIPSET_STATE_SOFT_OFF;
	case SYS_POWER_STATE_S3:
	case SYS_POWER_STATE_S4:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S5S4:
	case SYS_POWER_STATE_S4S3:
	case SYS_POWER_STATE_S0S3:
		return state_mask & AP_PWRSEQ_CHIPSET_STATE_SUSPEND;
	case SYS_POWER_STATE_S0:
	case SYS_POWER_STATE_S3S0:
		return state_mask & AP_PWRSEQ_CHIPSET_STATE_ON;
	/* TODO: b/203446865 S0ix */
	}
	/* Unknown power state; return false. */
	return 0;
}

void ap_pwrseq_chipset_exit_hardoff(void)
{
	enum power_states_ndsx power_state;

	/*
	 * If not in the soft-off state, hard-off state, or headed there,
	 * nothing to do.
	 */
	power_state = pwr_sm_get_state();
	if (power_state != SYS_POWER_STATE_G3 &&
	    power_state != SYS_POWER_STATE_S5G3 &&
	    power_state != SYS_POWER_STATE_S5)
		return;
	chipset_request_exit_hardoff(true);
}
