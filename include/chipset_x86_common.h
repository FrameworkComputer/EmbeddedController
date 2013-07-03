/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common interface for x86 chipsets */

#ifndef __CROS_EC_CHIPSET_X86_COMMON_H
#define __CROS_EC_CHIPSET_X86_COMMON_H

#include "common.h"
#include "gpio.h"

enum x86_state {
	/* Steady states */
	X86_G3 = 0,	/*
			 * System is off (not technically all the way into G3,
			 * which means totally unpowered...)
			 */
	X86_S5,		/* System is soft-off */
	X86_S3,		/* Suspend; RAM on, processor is asleep */
	X86_S0,		/* System is on */

	/* Transitions */
	X86_G3S5,	/* G3 -> S5 (at system init time) */
	X86_S5S3,	/* S5 -> S3 */
	X86_S3S0,	/* S3 -> S0 */
	X86_S0S3,	/* S0 -> S3 */
	X86_S3S5,	/* S3 -> S5 */
	X86_S5G3,	/* S5 -> G3 */
};

/* Information on an x86 signal */
struct x86_signal_info {
	enum gpio_signal gpio;	/* GPIO for signal */
	int level;		/* GPIO level which sets signal bit */
	const char *name;	/* Name of signal */
};

/*
 * Each board must provide its signal list and a corresponding enum x86_signal.
 */
extern const struct x86_signal_info x86_signal_list[];

/* Convert enum x86_signal to a mask for signal functions */
#define X86_SIGNAL_MASK(signal) (1 << (signal))

/**
 * Return current input signal state (one or more X86_SIGNAL_MASK()s).
 */
uint32_t x86_get_signals(void);

/**
 * Check for required inputs
 *
 * @param want		Mask of signals which must be present (one or more
 *			X86_SIGNAL_MASK()s).
 *
 * @return Non-zero if all present; zero if a required signal is missing.
 */
int x86_has_signals(uint32_t want);

/**
 * Wait for x86 input signals to be present
 *
 * @param want		Mask of signals which must be present (one or more
 *			X86_SIGNAL_MASK()s).  If want=0, stops waiting for
 *			signals.
 * @return EC_SUCCESS when all inputs are present, or ERROR_TIMEOUT if timeout
 * before reaching the desired state.
 */
int x86_wait_signals(uint32_t want);

/**
 * Chipset-specific initialization
 *
 * @return The state the chipset should start in.  Usually X86_G3, but may
 * be X86_G0 if the chipset was already on and we've jumped to this image.
 */
enum x86_state x86_chipset_init(void);

/**
 * Chipset-specific state handler
 *
 * @return The updated state for the x86 chipset.
 */
enum x86_state x86_handle_state(enum x86_state state);

/**
 * Interrupt handler for x86 chipset GPIOs.
 */
void x86_interrupt(enum gpio_signal signal);

#endif  /* __CROS_EC_CHIPSET_X86_COMMON_H */
