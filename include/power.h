/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common power interface for all chipsets */

#ifndef __CROS_EC_POWER_H
#define __CROS_EC_POWER_H

#include "common.h"
#include "gpio.h"
#include "task_id.h"

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

/*
 * Power signal flags:
 *
 * +-----------------+------------------------------------+
 * |     Bit #       |           Description              |
 * +------------------------------------------------------+
 * |       0         |      Active level (low/high)       |
 * +------------------------------------------------------+
 * |       1         |    Signal interrupt state at boot  |
 * +------------------------------------------------------+
 * |     2 : 32      |            Reserved                |
 * +-----------------+------------------------------------+
 */

#define POWER_SIGNAL_ACTIVE_STATE	BIT(0)
#define POWER_SIGNAL_ACTIVE_LOW	(0 << 0)
#define POWER_SIGNAL_ACTIVE_HIGH	BIT(0)

#define POWER_SIGNAL_INTR_STATE	BIT(1)
#define POWER_SIGNAL_DISABLE_AT_BOOT	BIT(1)

/* Information on an power signal */
struct power_signal_info {
	enum gpio_signal gpio;	/* GPIO for signal */
	uint32_t flags;		/* See POWER_SIGNAL_* macros */
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
 * Check if provided power signal is currently asserted.
 *
 * @param s		Power signal that needs to be checked.
 *
 * @return 1 if power signal is asserted, 0 otherwise.
 */
int power_signal_is_asserted(const struct power_signal_info *s);

/**
 * Get the level of provided input signal.
 */
int power_signal_get_level(enum gpio_signal signal);

/**
 * Enable interrupt for provided input signal.
 */
int power_signal_enable_interrupt(enum gpio_signal signal);

/**
 * Disable interrupt for provided input signal.
 */
int power_signal_disable_interrupt(enum gpio_signal signal);

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
 * Set the low-level chipset power state.
 *
 * @return Current chipset power state
 */
enum power_state power_get_state(void);

/*
 * Set the wake mask according to the current power state.
 */
void power_update_wake_mask(void);

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
 * Interrupt handler for rsmrst signal GPIO. This interrupt handler should be
 * used when there is a requirement to have minimum pass through delay between
 * the rsmrst coming to the EC and the rsmrst that goes to the PCH for high->low
 * transitions. Low->high transitions are still handled from within the chipset
 * task power state machine.
 *
 * @param signal - The gpio signal that triggered the interrupt.
 */
void intel_x86_rsmrst_signal_interrupt(enum gpio_signal signal);

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

/**
 * Set sleep state of host.
 *
 * @param state The new state to set.
 */
void power_set_host_sleep_state(enum host_sleep_event state);

/* Context to pass to a host sleep command handler. */
struct host_sleep_event_context {
	uint32_t sleep_transitions; /* Number of sleep transitions observed */
	uint16_t sleep_timeout_ms;  /* Timeout in milliseconds */
};

/**
 * Provide callback to allow chipset to take any action on host sleep event
 * command.
 *
 * @param state Current host sleep state updated by the host.
 * @param ctx Possible sleep parameters and return values, depending on state.
 */
__override_proto void power_chipset_handle_host_sleep_event(
		enum host_sleep_event state,
		struct host_sleep_event_context *ctx);

/**
 * Provide callback to allow board to take any action on host sleep event
 * command.
 *
 * @param state Current host sleep state updated by the host.
 */
__override_proto void power_board_handle_host_sleep_event(
		enum host_sleep_event state);

/*
 * This is the default state of host sleep event. Calls to
 * power_reset_host_sleep_state will set host sleep event to this
 * value. EC components listening to host sleep event updates can check for this
 * special value to know if the state was reset.
 */
#define HOST_SLEEP_EVENT_DEFAULT_RESET		0

#ifdef CONFIG_POWER_S0IX
/**
 * Reset the sleep state reported by the host.
 *
 * @param sleep_event Reset sleep state.
 */
void power_reset_host_sleep_state(void);
#endif /* CONFIG_POWER_S0IX */
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

/**
 * Enable/Disable the PP5000 rail.
 *
 * This function will turn on the 5V rail immediately if requested.  However,
 * the rail will not turn off until all tasks want it off.
 *
 * NOTE: Be careful when calling from deferred functions, as they will all be
 * executed within the same task context! (The HOOKS task).
 *
 * @param tid: The caller's task ID.
 * @param enable: 1 to turn on the rail, 0 to request the rail to be turned off.
 */
void power_5v_enable(task_id_t tid, int enable);

#endif  /* __CROS_EC_POWER_H */
