/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset module for Chrome EC.
 *
 * This is intended to be a platform/chipset-neutral interface, implemented by
 * all main chipsets (x86, gaia, etc.). */

#ifndef __CROS_EC_CHIPSET_H
#define __CROS_EC_CHIPSET_H

#include "common.h"

/* Chipset state mask
 *
 * Note that this is a non-exhaustive list of states which the main chipset can
 * be in, and is potentially one-to-many for real, underlying chipset states.
 * That's why chipset_in_state() asks "Is the chipset in something
 * approximating this state?" and not "Tell me what state the chipset is in and
 * I'll compare it myself with the state(s) I want." */
enum chipset_state_mask {
	CHIPSET_STATE_HARD_OFF = 0x01,   /* Hard off (G3) */
	CHIPSET_STATE_SOFT_OFF = 0x02,   /* Soft off (S5) */
	CHIPSET_STATE_SUSPEND  = 0x04,   /* Suspend (S3) */
	CHIPSET_STATE_ON       = 0x08,   /* On (S0) */
	/* Common combinations */
	CHIPSET_STATE_ANY_OFF = (CHIPSET_STATE_HARD_OFF |
				 CHIPSET_STATE_SOFT_OFF),  /* Any off state */
};

/* Return non-zero if the chipset is in one of the states specified in the
 * mask. */
int chipset_in_state(int state_mask);

/* Ask the chipset to exit the hard off state.  Does nothing if the chipset has
 * already left the state, or was not in the state to begin with. */
void chipset_exit_hard_off(void);

#endif  /* __CROS_EC_CHIPSET_H */
