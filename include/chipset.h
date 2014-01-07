/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Chipset module for Chrome EC.
 *
 * This is intended to be a platform/chipset-neutral interface, implemented by
 * all main chipsets (x86, gaia, etc.).
 */

#ifndef __CROS_EC_CHIPSET_H
#define __CROS_EC_CHIPSET_H

#include "common.h"

/*
 * Chipset state mask
 *
 * Note that this is a non-exhaustive list of states which the main chipset can
 * be in, and is potentially one-to-many for real, underlying chipset states.
 * That's why chipset_in_state() asks "Is the chipset in something
 * approximating this state?" and not "Tell me what state the chipset is in and
 * I'll compare it myself with the state(s) I want."
 */
enum chipset_state_mask {
	CHIPSET_STATE_HARD_OFF = 0x01,   /* Hard off (G3) */
	CHIPSET_STATE_SOFT_OFF = 0x02,   /* Soft off (S5) */
	CHIPSET_STATE_SUSPEND  = 0x04,   /* Suspend (S3) */
	CHIPSET_STATE_ON       = 0x08,   /* On (S0) */
	/* Common combinations */
	CHIPSET_STATE_ANY_OFF = (CHIPSET_STATE_HARD_OFF |
				 CHIPSET_STATE_SOFT_OFF),  /* Any off state */
};

#ifdef HAS_TASK_CHIPSET

/**
 * Check if chipset is in a given state.
 *
 * @param state_mask	Combination of one or more CHIPSET_STATE_* flags.
 *
 * @return non-zero if the chipset is in one of the states specified in the
 * mask.
 */
int chipset_in_state(int state_mask);

/**
 * Ask the chipset to exit the hard off state.
 *
 * Does nothing if the chipset has already left the state, or was not in the
 * state to begin with.
 */
void chipset_exit_hard_off(void);

/* This is a private chipset-specific implementation for use only by
 * throttle_ap() . Don't call this directly!
 */
void chipset_throttle_cpu(int throttle);

/**
 * Immediately shut off power to main processor and chipset.
 *
 * This is intended for use when the system is too hot or battery power is
 * critical.
 */
void chipset_force_shutdown(void);

/**
 * Reset the CPU and/or chipset.
 *
 * @param cold_reset	If !=0, force a cold reset of the CPU and chipset;
 *			if 0, just pulse the reset line to the CPU.
 */
void chipset_reset(int cold_reset);

/**
 * Interrupt handler to power GPIO inputs.
 */
void power_interrupt(enum gpio_signal signal);

#else /* !HAS_TASK_CHIPSET */
/*
 * Allow other modules to compile if the chipset module is disabled.  This is
 * commonly done during early stages of board bringup.
 */

static inline int chipset_in_state(int state_mask)
{
	return 0;
}

static inline void chipset_exit_hard_off(void) { }
static inline void chipset_throttle_cpu(int throttle) { }
static inline void chipset_force_shutdown(void) { }
static inline void chipset_reset(int cold_reset) { }

#define power_interrupt NULL

#endif /* !HAS_TASK_CHIPSET */

#endif  /* __CROS_EC_CHIPSET_H */
