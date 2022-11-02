/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ap_power/ap_power_interface.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#ifndef CONFIG_AP_PWRSEQ_DRIVER
bool ap_power_in_state(enum ap_power_state_mask state_mask)
{
	int need_mask = 0;

	switch (pwr_sm_get_state()) {
	case SYS_POWER_STATE_UNINIT:
		LOG_WRN("%s: init not yet complete; AP state is unknown",
			__func__);
		return false;
	case SYS_POWER_STATE_G3:
		need_mask = AP_POWER_STATE_HARD_OFF;
		break;
	case SYS_POWER_STATE_G3S5:
	case SYS_POWER_STATE_S5G3:
		/*
		 * In between hard and soft off states.  Match only if caller
		 * will accept both.
		 */
		need_mask = AP_POWER_STATE_HARD_OFF | AP_POWER_STATE_SOFT_OFF;
		break;
	case SYS_POWER_STATE_S5:
		need_mask = AP_POWER_STATE_SOFT_OFF;
		break;
	case SYS_POWER_STATE_S5S4:
	case SYS_POWER_STATE_S4S5:
		need_mask = AP_POWER_STATE_SOFT_OFF | AP_POWER_STATE_SUSPEND;
		break;
	case SYS_POWER_STATE_S4:
	case SYS_POWER_STATE_S4S3:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S3:
		need_mask = AP_POWER_STATE_SUSPEND;
		break;
	case SYS_POWER_STATE_S3S0:
	case SYS_POWER_STATE_S0S3:
		need_mask = AP_POWER_STATE_SUSPEND | AP_POWER_STATE_ON;
		break;
	case SYS_POWER_STATE_S0:
		need_mask = AP_POWER_STATE_ON;
		break;
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ixS0:
	case SYS_POWER_STATE_S0S0ix:
		need_mask = AP_POWER_STATE_ON | AP_POWER_STATE_STANDBY;
		break;
	case SYS_POWER_STATE_S0ix:
		need_mask = AP_POWER_STATE_STANDBY;
		break;
#endif
	}
	/* Return non-zero if all needed bits are present */
	return (state_mask & need_mask) == need_mask;
}

bool ap_power_in_or_transitioning_to_state(enum ap_power_state_mask state_mask)
{
	switch (pwr_sm_get_state()) {
	case SYS_POWER_STATE_UNINIT:
		LOG_WRN("%s: init not yet complete; AP state is unknown",
			__func__);
		return 0;
	case SYS_POWER_STATE_G3:
	case SYS_POWER_STATE_S5G3:
		return state_mask & AP_POWER_STATE_HARD_OFF;
	case SYS_POWER_STATE_S5:
	case SYS_POWER_STATE_G3S5:
	case SYS_POWER_STATE_S4S5:
		return state_mask & AP_POWER_STATE_SOFT_OFF;
	case SYS_POWER_STATE_S3:
	case SYS_POWER_STATE_S4:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S5S4:
	case SYS_POWER_STATE_S4S3:
	case SYS_POWER_STATE_S0S3:
		return state_mask & AP_POWER_STATE_SUSPEND;
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ix:
	case SYS_POWER_STATE_S0S0ix:
		return state_mask & AP_POWER_STATE_STANDBY;
#endif
	case SYS_POWER_STATE_S0:
	case SYS_POWER_STATE_S3S0:
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ixS0:
#endif
		return state_mask & AP_POWER_STATE_ON;
	}
	/* Unknown power state; return false. */
	return 0;
}

void ap_power_exit_hardoff(void)
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
	request_start_from_g3();
}
#else
bool ap_power_in_state(enum ap_power_state_mask state_mask)
{
	/*
	 * PWRSEQ_DRIVER will only expose stable power states (transitions
	 * occur automatically and are not visible to API consumers), so
	 * return ap_power_in_or_transitioning_to_state since it is
	 * equivalent to this function.
	 */
	return ap_power_in_or_transitioning_to_state(state_mask);
}

bool ap_power_in_or_transitioning_to_state(enum ap_power_state_mask state_mask)
{
	const struct device *dev = ap_pwrseq_get_instance();

	switch (ap_pwrseq_get_current_state(dev)) {
	case AP_POWER_STATE_G3:
		return state_mask & AP_POWER_STATE_HARD_OFF;
	case AP_POWER_STATE_S5:
		return state_mask & AP_POWER_STATE_SOFT_OFF;
	case AP_POWER_STATE_S3:
	case AP_POWER_STATE_S4:
		return state_mask & AP_POWER_STATE_SUSPEND;
#if CONFIG_AP_PWRSEQ_S0IX
	case AP_POWER_STATE_S0IX:
		return state_mask & AP_POWER_STATE_STANDBY;
#endif
	case AP_POWER_STATE_S0:
		return state_mask & AP_POWER_STATE_ON;
	default:
		break;
	}
	/* Unknown power state; return false. */
	return 0;
}

void ap_power_exit_hardoff(void)
{
	const struct device *dev = ap_pwrseq_get_instance();
	enum ap_pwrseq_state power_state;

	ap_pwrseq_state_lock(dev);
	/*
	 * If not in the soft-off state, hard-off state, or headed there,
	 * nothing to do.
	 */
	power_state = ap_pwrseq_get_current_state(dev);
	if (power_state == AP_POWER_STATE_G3 ||
	    power_state == AP_POWER_STATE_S5) {
		request_start_from_g3();
	}

	ap_pwrseq_state_unlock(dev);
}

void ap_pwrseq_task_start(void)
{
	const struct device *dev = ap_pwrseq_get_instance();

	ap_pwrseq_start(dev, chipset_pwr_seq_get_state());
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

void ap_power_init_reset_log(void)
{
}
