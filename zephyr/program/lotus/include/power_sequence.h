/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWERSEQUENCE_H
#define __CROS_EC_POWERSEQUENCE_H

/* Power signals list */
enum power_signal {
	X86_3VALW_PG,
	X86_SLP_S3_N,
	X86_SLP_S5_N,
	X86_VR_PG,
	POWER_SIGNAL_COUNT
};

int get_power_rail_status(void);

void power_s5_up_control(int control);

void power_state_clear(int state);

#endif	/* __CROS_EC_POWERSEQUENCE_H */
