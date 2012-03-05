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

/* Chipset state.
 *
 * Note that this is a non-exhaustive list of states which the main chipset can
 * be in, and is potentially one-to-many for real, underlying chipset states.
 * That's why chipset_in_state() asks "Is the chipset in something
 * approximating this state?" and not "Tell me what state the chipset is in and
 * I'll compare it myself with the state(s) I want." */
enum chipset_state {
	CHIPSET_STATE_SOFT_OFF,   /* Soft off (S5) */
	CHIPSET_STATE_SUSPEND,    /* Suspend (S3) */
	CHIPSET_STATE_ON,         /* On (S0) */
};

/* Returns non-zero if the chipset is in the specified state. */
int chipset_in_state(enum chipset_state in_state);

#endif  /* __CROS_EC_CHIPSET_H */
