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
#include "gpio.h"

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
	CHIPSET_STATE_STANDBY  = 0x10,   /* Standby (S0ix) */
	/* Common combinations */
	CHIPSET_STATE_ANY_OFF = (CHIPSET_STATE_HARD_OFF |
				 CHIPSET_STATE_SOFT_OFF),  /* Any off state */
	/* This combination covers any kind of suspend i.e. S3 or S0ix. */
	CHIPSET_STATE_ANY_SUSPEND = (CHIPSET_STATE_SUSPEND |
				     CHIPSET_STATE_STANDBY),
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
 */
void chipset_reset(void);

/**
 * Interrupt handler to power GPIO inputs.
 */
void power_interrupt(enum gpio_signal signal);

/**
 * Handle assert of eSPI_Reset# pin.
 */
void chipset_handle_espi_reset_assert(void);

#else /* !HAS_TASK_CHIPSET */

/* When no chipset is present, assume it is always off. */
static inline int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}

static inline void chipset_exit_hard_off(void) { }
static inline void chipset_throttle_cpu(int throttle) { }
static inline void chipset_force_shutdown(void) { }
static inline void chipset_reset(void) { }
static inline void power_interrupt(enum gpio_signal signal) { }
static inline void chipset_handle_espi_reset_assert(void) { }
static inline void chipset_handle_reboot(void) { }

#endif /* !HAS_TASK_CHIPSET */

/**
 * Optional chipset check if PLTRST# is valid.
 *
 * @return non-zero if PLTRST# is valid, 0 if invalid.
 */
int chipset_pltrst_is_valid(void) __attribute__((weak));

/**
 * Execute chipset-specific reboot.
 */
void chipset_handle_reboot(void);
#endif  /* __CROS_EC_CHIPSET_H */
