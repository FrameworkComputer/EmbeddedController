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
#ifdef CONFIG_BOARD_MARIGOLD
	X86_PRIM_PWR,
	X86_SLP_S4_N,
#endif
	POWER_SIGNAL_COUNT
};

/* s0ix entry/exit flag state */
enum s0ix_state {
	CS_NONE,
	CS_ENTER_S0ix,
	CS_EXIT_S0ix,
};

int get_power_rail_status(void);

void clear_power_flags(void);

void power_s5_up_control(int control);

void power_state_clear(int state);

int check_s0ix_status(void);

#endif	/* __CROS_EC_POWERSEQUENCE_H */
