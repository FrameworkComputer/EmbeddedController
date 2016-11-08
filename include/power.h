/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common power interface for all chipsets */

#ifndef __CROS_EC_POWER_H
#define __CROS_EC_POWER_H

#include "common.h"
#include "gpio.h"

enum power_state {
	/* Steady states */
	POWER_G3 = 0,	/*
			 * System is off (not technically all the way into G3,
			 * which means totally unpowered...)
			 */
	POWER_S5,		/* System is soft-off */
	POWER_S3,		/* Suspend; RAM on, processor is asleep */
	POWER_S0,		/* System is on */
#ifdef CONFIG_POWER_S0IX
	POWER_S0ix,
#endif
	/* Transitions */
	POWER_G3S5,	/* G3 -> S5 (at system init time) */
	POWER_S5S3,	/* S5 -> S3 */
	POWER_S3S0,	/* S3 -> S0 */
	POWER_S0S3,	/* S0 -> S3 */
	POWER_S3S5,	/* S3 -> S5 */
	POWER_S5G3,	/* S5 -> G3 */
#ifdef CONFIG_POWER_S0IX
	POWER_S0ixS0,   /* S0ix -> S0 */
	POWER_S0S0ix,   /* S0 -> S0ix */
#endif
};

/* Information on an power signal */
struct power_signal_info {
	enum gpio_signal gpio;	/* GPIO for signal */
	int level;		/* GPIO level which sets signal bit */
	const char *name;	/* Name of signal */
};

/*
 * Each board must provide its signal list and a corresponding enum
 * power_signal.
 */
extern const struct power_signal_info power_signal_list[];

/* Convert enum power_signal to a mask for signal functions */
#define POWER_SIGNAL_MASK(signal) (1 << (signal))

/**
 * Return current input signal state (one or more POWER_SIGNAL_MASK()s).
 */
uint32_t power_get_signals(void);

/**
 * Check for required inputs
 *
 * @param want		Mask of signals which must be present (one or more
 *			POWER_SIGNAL_MASK()s).
 *
 * @return Non-zero if all present; zero if a required signal is missing.
 */
int power_has_signals(uint32_t want);

/**
 * Wait for power input signals to be present using default timeout
 *
 * @param want		Mask of signals which must be present (one or more
 *			POWER_SIGNAL_MASK()s).  If want=0, stops waiting for
 *			signals.
 * @return EC_SUCCESS when all inputs are present, or ERROR_TIMEOUT if timeout
 * before reaching the desired state.
 */
int power_wait_signals(uint32_t want);

/**
 * Wait for power input signals to be present
 *
 * @param want		Mask of signals which must be present (one or more
 *			POWER_SIGNAL_MASK()s).  If want=0, stops waiting for
 *			signals.
 * @param timeout       Timeout in usec to wait for signals to be present.
 * @return EC_SUCCESS when all inputs are present, or ERROR_TIMEOUT if timeout
 * before reaching the desired state.
 */
int power_wait_signals_timeout(uint32_t want, int timeout);

/**
 * Set the low-level power chipset state.
 *
 * @param new_state New chipset state.
 */
void power_set_state(enum power_state new_state);

/**
 * Chipset-specific initialization
 *
 * @return The state the chipset should start in.  Usually POWER_G3, but may
 * be POWER_G0 if the chipset was already on and we've jumped to this image.
 */
enum power_state power_chipset_init(void);

/**
 * Chipset-specific state handler
 *
 * @return The updated state for the chipset.
 */
enum power_state power_handle_state(enum power_state state);

/**
 * Interrupt handler for power signal GPIOs.
 */
#ifdef HAS_TASK_CHIPSET
void power_signal_interrupt(enum gpio_signal signal);
#else
static inline void power_signal_interrupt(enum gpio_signal signal) { }
#endif /* !HAS_TASK_CHIPSET */

/**
 * pause_in_s5 getter method.
 *
 * @return Whether we should pause in S5 when shutting down.
 */
int power_get_pause_in_s5(void);

/**
 * pause_in_s5 setter method.
 *
 * @param pause True if we should pause in S5 when shutting down.
 */
void power_set_pause_in_s5(int pause);

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
/**
 * Get sleep state of host, as reported by the host.
 *
 * @return Believed sleep state of host.
 */
enum host_sleep_event power_get_host_sleep_state(void);

#ifdef CONFIG_POWER_S0IX
/**
 * Reset the sleep state reported by the host.
 *
 * @param sleep_event Reset sleep state.
 */
void power_reset_host_sleep_state(enum host_sleep_event sleep_event);
#endif /* CONFIG_POWER_S0IX */
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

#endif  /* __CROS_EC_POWER_H */
