/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __AP_PWRSEQ_CHIPSET_H__
#define __AP_PWRSEQ_CHIPSET_H__

#include <stdbool.h>

enum ap_pwrseq_chipset_state_mask {
	AP_PWRSEQ_CHIPSET_STATE_HARD_OFF = 0x01,   /* Hard off (G3) */
	AP_PWRSEQ_CHIPSET_STATE_SOFT_OFF = 0x02,   /* Soft off (S5, S4) */
	AP_PWRSEQ_CHIPSET_STATE_SUSPEND  = 0x04,   /* Suspend (S3) */
	AP_PWRSEQ_CHIPSET_STATE_ON       = 0x08,   /* On (S0) */
	AP_PWRSEQ_CHIPSET_STATE_STANDBY  = 0x10,   /* Standby (S0ix) */
	/* Common combinations, any off state */
	AP_PWRSEQ_CHIPSET_STATE_ANY_OFF = (AP_PWRSEQ_CHIPSET_STATE_HARD_OFF |
				 AP_PWRSEQ_CHIPSET_STATE_SOFT_OFF),
	/* This combination covers any kind of suspend i.e. S3 or S0ix. */
	AP_PWRSEQ_CHIPSET_STATE_ANY_SUSPEND = (AP_PWRSEQ_CHIPSET_STATE_SUSPEND |
				     AP_PWRSEQ_CHIPSET_STATE_STANDBY),
};

/**
 * Check if chipset is in a given state.
 *
 * @param state_mask Combination of one or more AP_PWRSEQ_CHIPSET_STATE_*
 *                      flags.
 *
 * @return non-zero if the chipset is in one of the states specified in the
 * mask.
 */
bool ap_pwrseq_chipset_in_state(
		enum ap_pwrseq_chipset_state_mask state_mask);

/**
 * Check if chipset is in a given state or if the chipset task is currently
 * transitioning to that state. For example, G3S5, S5, and S3S5 would all count
 * as the S5 state.
 *
 * @param state_mask Combination of one or more AP_PWRSEQ_CHIPSET_STATE_* flags.
 *
 * @return true if the chipset is in one of the states specified in the
 * mask.
 */
bool ap_pwrseq_chipset_in_or_transitioning_to_state(
		enum ap_pwrseq_chipset_state_mask state_mask);

/**
 * Ask the chipset to exit the hard off state.
 *
 * Does nothing if the chipset has already left the state, or was not in the
 * state to begin with.
 */
void ap_pwrseq_chipset_exit_hardoff(void);

#endif /* __AP_PWRSEQ_CHIPSET_H__ */
