/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief External API interface to AP power sequence subsystem.
 *
 * Defines the interface to the AP power sequence module,
 * which includes:
 *  - enums for the power state of the AP
 *  - enums for the power state mask of the AP
 *  - enums indicating the reason for shutdown
 *  - enums for providing control values
 *  - function declarations for getting the state of the AP
 *  - function declarations for requesting actions
 *
 * These definitions are roughly equivalent to the legacy
 * chipset API, but are separate to ensure there is no
 * reference to the legacy API. To reflect this,
 * equivalent functions are prepended with ap_power_ instead
 * of chipset_.
 */

#ifndef __AP_POWER_AP_POWER_INTERFACE_H__
#define __AP_POWER_AP_POWER_INTERFACE_H__

#include <sys/util.h>

/**
 * @brief System power states for Non Deep Sleep Well
 * EC is an always on device in a Non Deep Sx system except when EC
 * is hibernated or all the VRs are turned off.
 */
enum power_states_ndsx {
	/*
	 * Actual power states
	 */
	/* AP is off & EC is on */
	SYS_POWER_STATE_G3,
	/* AP is in soft off state */
	SYS_POWER_STATE_S5,
	/* AP is suspended to Non-volatile disk */
	SYS_POWER_STATE_S4,
	/* AP is suspended to RAM */
	SYS_POWER_STATE_S3,
	/* AP is in active state */
	SYS_POWER_STATE_S0,
	/*
	 * Intermediate power up states
	 */
	/* Determine if the AP's power rails are turned on */
	SYS_POWER_STATE_G3S5,
	/* Determine if AP is suspended from sleep */
	SYS_POWER_STATE_S5S4,
	/* Determine if Suspend to Disk is de-asserted */
	SYS_POWER_STATE_S4S3,
	/* Determine if Suspend to RAM is de-asserted */
	SYS_POWER_STATE_S3S0,
	/*
	 * Intermediate power down states
	 */
	/* Determine if the AP's power rails are turned off */
	SYS_POWER_STATE_S5G3,
	/* Determine if AP is suspended to sleep */
	SYS_POWER_STATE_S4S5,
	/* Determine if Suspend to Disk is asserted */
	SYS_POWER_STATE_S3S4,
	/* Determine if Suspend to RAM is asserted */
	SYS_POWER_STATE_S0S3,
};

/**
 * @brief Represents the state of the AP as a mask.
 */
enum ap_power_state_mask {
	AP_POWER_STATE_HARD_OFF = BIT(0),   /* Hard off (G3) */
	AP_POWER_STATE_SOFT_OFF = BIT(1),   /* Soft off (S5, S4) */
	AP_POWER_STATE_SUSPEND  = BIT(2),   /* Suspend (S3) */
	AP_POWER_STATE_ON       = BIT(3),   /* On (S0) */
	AP_POWER_STATE_STANDBY  = BIT(4),   /* Standby (S0ix) */
	/* Common combinations, any off state */
	AP_POWER_STATE_ANY_OFF = (AP_POWER_STATE_HARD_OFF |
				 AP_POWER_STATE_SOFT_OFF),
	/* This combination covers any kind of suspend i.e. S3 or S0ix. */
	AP_POWER_STATE_ANY_SUSPEND = (AP_POWER_STATE_SUSPEND |
				     AP_POWER_STATE_STANDBY),
};

/**
 * @brief AP shutdown reason codes
 *
 * These enums <em>MUST</em> be kept as the same corresponding values
 * as the values in
 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/include/ec_commands.h
 * since they are referenced in external programs.
 * Before changing any of these values, read the comments
 * in ec_command.h
 */
enum ap_power_shutdown_reason {
	/*
	 * Beginning of reset reasons.
	 */
	AP_POWER_RESET_BEGIN = 0,
	AP_POWER_RESET_UNKNOWN = AP_POWER_RESET_BEGIN,
	/* Custom reason defined by a board.c or baseboard.c file */
	AP_POWER_RESET_BOARD_CUSTOM,
	/* Believe that the AP has hung */
	AP_POWER_RESET_HANG_REBOOT,
	/* Reset by EC console command */
	AP_POWER_RESET_CONSOLE_CMD,
	/* Reset by EC host command */
	AP_POWER_RESET_HOST_CMD,
	/* Keyboard module reset key combination */
	AP_POWER_RESET_KB_SYSRESET,
	/* Keyboard module warm reboot */
	AP_POWER_RESET_KB_WARM_REBOOT,
	/* Debug module warm reboot */
	AP_POWER_RESET_DBG_WARM_REBOOT,
	/* I cannot self-terminate.  You must lower me into the steel. */
	AP_POWER_RESET_AP_REQ,
	/* Reset as side-effect of startup sequence */
	AP_POWER_RESET_INIT,
	/* EC detected an AP watchdog event. */
	AP_POWER_RESET_AP_WATCHDOG,

	AP_POWER_RESET_COUNT, /* End of reset reasons. */

	/*
	 * Beginning of shutdown reasons.
	 */
	AP_POWER_SHUTDOWN_BEGIN = BIT(15),
	AP_POWER_SHUTDOWN_POWERFAIL = AP_POWER_SHUTDOWN_BEGIN,
	/* Forcing a shutdown as part of EC initialization */
	AP_POWER_SHUTDOWN_INIT,
	/* Custom reason on a per-board basis. */
	AP_POWER_SHUTDOWN_BOARD_CUSTOM,
	/* This is a reason to inhibit startup, not cause shut down. */
	AP_POWER_SHUTDOWN_BATTERY_INHIBIT,
	/* A power_wait_signal is being asserted */
	AP_POWER_SHUTDOWN_WAIT,
	/* Critical battery level. */
	AP_POWER_SHUTDOWN_BATTERY_CRIT,
	/* Because you told me to. */
	AP_POWER_SHUTDOWN_CONSOLE_CMD,
	/* Forcing a shutdown to effect entry to G3. */
	AP_POWER_SHUTDOWN_G3,
	/* Force shutdown due to over-temperature. */
	AP_POWER_SHUTDOWN_THERMAL,
	/* Force a AP shutdown from the power button through EC */
	AP_POWER_SHUTDOWN_BUTTON,

	AP_POWER_SHUTDOWN_COUNT, /* End of shutdown reasons. */
};

/**
 * @brief Check if AP is in a given state.
 *
 * @param state_mask Combination of one or more AP_POWER_STATE_* flags.
 * @return non-zero if the AP is in one of the states specified in the mask.
 */
bool ap_power_in_state(enum ap_power_state_mask state_mask);

/**
 * Check if AP is in a given state or if the AP task is currently
 * transitioning to that state. For example, G3S5, S5, and S3S5 would all count
 * as the S5 state.
 *
 * @param state_mask Combination of one or more AP_POWER_STATE_* flags.
 *
 * @return true if the AP is in one of the states specified in the
 * mask.
 */
bool ap_power_in_or_transitioning_to_state(enum ap_power_state_mask sm);

/**
 * @brief Ask the AP to exit the hard off state.
 *
 * Does nothing if the AP has already left the state, or was not in the
 * state to begin with.
 */
void ap_power_exit_hardoff(void);

/**
 * @brief Reset the AP
 *
 * @param reason The reason why the AP is being reset.
 */
void ap_power_reset(enum ap_power_shutdown_reason reason);

/**
 * @brief Immediately shut off power to the AP.
 *
 * This is intended for use when the system is too hot or battery power is
 * critical.
 *
 * @param reason The reason why the AP is being shut down.
 */
void ap_power_force_shutdown(enum ap_power_shutdown_reason reason);

/**
 * @brief Initialise the AP reset log.
 */
void ap_power_init_reset_log(void);

#endif /* __AP_POWER_AP_POWER_INTERFACE_H__ */
