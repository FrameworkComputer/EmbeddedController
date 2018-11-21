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

/*
 * Reason codes used by the AP after a shutdown to figure out why it was reset
 * by the EC.  These are sent in EC commands.  Therefore, to maintain protocol
 * compatibility:
 * - New entries must be inserted prior to the _COUNT field
 * - If an existing entry is no longer in service, it must be replaced with a
 *   RESERVED entry instead.
 * - The semantic meaning of an entry should not change.
 * - Do not exceed 2^15 - 1 for reset reasons or 2^16 - 1 for shutdown reasons.
 */
enum chipset_reset_reason {
	CHIPSET_RESET_BEGIN = 0,
	CHIPSET_RESET_UNKNOWN = CHIPSET_RESET_BEGIN,
	/* Custom reason defined by a board.c or baseboard.c file */
	CHIPSET_RESET_BOARD_CUSTOM,
	/* Believe that the AP has hung */
	CHIPSET_RESET_HANG_REBOOT,
	/* Reset by EC console command */
	CHIPSET_RESET_CONSOLE_CMD,
	/* Reset by EC host command */
	CHIPSET_RESET_HOST_CMD,
	/* Keyboard module reset key combination */
	CHIPSET_RESET_KB_SYSRESET,
	/* Keyboard module warm reboot */
	CHIPSET_RESET_KB_WARM_REBOOT,
	/* Debug module warm reboot */
	CHIPSET_RESET_DBG_WARM_REBOOT,
	/* I cannot self-terminate.  You must lower me into the steel. */
	CHIPSET_RESET_AP_REQ,
	/* Reset as side-effect of startup sequence */
	CHIPSET_RESET_INIT,
	/* EC detected an AP watchdog event. */
	CHIPSET_RESET_AP_WATCHDOG,
	CHIPSET_RESET_COUNT,
};

/*
 * Hard shutdowns are logged on the same path as resets.
 */
enum chipset_shutdown_reason {
	CHIPSET_SHUTDOWN_BEGIN = 1 << 15,
	CHIPSET_SHUTDOWN_POWERFAIL = CHIPSET_SHUTDOWN_BEGIN,
	/* Forcing a shutdown as part of EC initialization */
	CHIPSET_SHUTDOWN_INIT,
	/* Custom reason on a per-board basis. */
	CHIPSET_SHUTDOWN_BOARD_CUSTOM,
	/* This is a reason to inhibit startup, not cause shut down. */
	CHIPSET_SHUTDOWN_BATTERY_INHIBIT,
	/* A power_wait_signal is being asserted */
	CHIPSET_SHUTDOWN_WAIT,
	/* Critical battery level. */
	CHIPSET_SHUTDOWN_BATTERY_CRIT,
	/* Because you told me to. */
	CHIPSET_SHUTDOWN_CONSOLE_CMD,
	/* Forcing a shutdown to effect entry to G3. */
	CHIPSET_SHUTDOWN_G3,
	/* Force shutdown due to over-temperature. */
	CHIPSET_SHUTDOWN_THERMAL,
	/* Force a chipset shutdown from the power button through EC */
	CHIPSET_SHUTDOWN_BUTTON,

	CHIPSET_SHUTDOWN_COUNT,
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
 * Check if chipset is in a given state or if the chipset task is currently
 * transitioning to that state. For example, G3S5, S5, and S3S5 would all count
 * as the S5 state.
 *
 * @param state_mask	Combination of one or more CHIPSET_STATE_* flags.
 *
 * @return non-zero if the chipset is in one of the states specified in the
 * mask.
 */
int chipset_in_or_transitioning_to_state(int state_mask);

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
void chipset_force_shutdown(enum chipset_shutdown_reason reason);

/**
 * Reset the CPU and/or chipset.
 */
void chipset_reset(enum chipset_reset_reason reason);

/**
 * Interrupt handler to power GPIO inputs.
 */
void power_interrupt(enum gpio_signal signal);

/**
 * Handle assert of eSPI_Reset# pin.
 */
void chipset_handle_espi_reset_assert(void);

/**
 * Perform chipset pre-initialization work within the context of chipset task.
 */
void chipset_pre_init_callback(void);

#else /* !HAS_TASK_CHIPSET */

/* When no chipset is present, assume it is always off. */
static inline int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}

static inline int chipset_in_or_transitioning_to_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}

static inline void chipset_exit_hard_off(void) { }
static inline void chipset_throttle_cpu(int throttle) { }
static inline void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
}

static inline void chipset_reset(enum chipset_reset_reason reason) { }
static inline void power_interrupt(enum gpio_signal signal) { }
static inline void chipset_handle_espi_reset_assert(void) { }
static inline void chipset_handle_reboot(void) { }
static inline void chipset_reset_request_interrupt(enum gpio_signal signal) { }
static inline void chipset_warm_reset_interrupt(enum gpio_signal signal) { }
static inline void chipset_watchdog_interrupt(enum gpio_signal signal) { }

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

/**
 * GPIO interrupt handler of reset request from AP.
 *
 * It is used in SDM845/MT8183 chipset power sequence.
 */
void chipset_reset_request_interrupt(enum gpio_signal signal);

/**
 * GPIO interrupt handler of warm reset signal from servo or H1.
 *
 * It is used in SDM845 chipset power sequence.
 */
void chipset_warm_reset_interrupt(enum gpio_signal signal);

/**
 * GPIO interrupt handler of watchdog from AP.
 *
 * It is used in MT8183 chipset, where it must be setup to trigger on falling
 * edge only.
 */
void chipset_watchdog_interrupt(enum gpio_signal signal);

#ifdef CONFIG_CMD_AP_RESET_LOG

/**
 * Report that the AP is being reset to the reset log.
 */
void report_ap_reset(enum chipset_shutdown_reason reason);

#else

static inline void report_ap_reset(enum chipset_shutdown_reason reason) { }

#endif /* !CONFIG_CMD_AP_RESET_LOG */

#endif  /* __CROS_EC_CHIPSET_H */
