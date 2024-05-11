/* Copyright 2012 The ChromiumOS Authors
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

#include "ap_reset_log.h"
#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "gpio_signal.h"
#include "stddef.h"

#ifdef __cplusplus
extern "C" {
#endif

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
	CHIPSET_STATE_HARD_OFF = 0x01, /* Hard off (G3) */
	CHIPSET_STATE_SOFT_OFF = 0x02, /* Soft off (S5, S4) */
	CHIPSET_STATE_SUSPEND = 0x04, /* Suspend (S3) */
	CHIPSET_STATE_ON = 0x08, /* On (S0) */
	CHIPSET_STATE_STANDBY = 0x10, /* Standby (S0ix) */
	/* Common combinations */
	CHIPSET_STATE_ANY_OFF =
		(CHIPSET_STATE_HARD_OFF | CHIPSET_STATE_SOFT_OFF), /* Any off
								      state */
	/* This combination covers any kind of suspend i.e. S3 or S0ix. */
	CHIPSET_STATE_ANY_SUSPEND =
		(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_STANDBY),
};

enum critical_shutdown {
	CRITICAL_SHUTDOWN_IGNORE,
	CRITICAL_SHUTDOWN_HIBERNATE,
	CRITICAL_SHUTDOWN_CUTOFF,
};

#ifdef CONFIG_AP_POWER_CONTROL

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
 * Attempt to power on the chipset if it's in S4/S5/G3.
 *
 * This does nothing if in S3/S0ix/S0.
 */
void chipset_power_on(void);

/**
 * Reset the CPU and/or chipset.
 */
void chipset_reset(enum chipset_shutdown_reason reason);

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

#else /* !CONFIG_AP_POWER_CONTROL */

#if defined(CONFIG_TEST_DISABLE_INLINE_CHIPSET_IN_STATE) || \
	defined(CONFIG_SOC_FAMILY_INTEL_ISH)
int chipset_in_state(int state_mask);

#else

/* When no chipset is present, assume it is always off. */
static inline int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}

#endif

static inline int chipset_in_or_transitioning_to_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}

static inline void chipset_exit_hard_off(void)
{
}
static inline void chipset_throttle_cpu(int throttle)
{
}
static inline void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
}

static inline void chipset_reset(enum chipset_shutdown_reason reason)
{
}
static inline void power_interrupt(enum gpio_signal signal)
{
}
static inline void chipset_handle_espi_reset_assert(void)
{
}
static inline void chipset_handle_reboot(void)
{
}
static inline void chipset_reset_request_interrupt(enum gpio_signal signal)
{
}
static inline void chipset_warm_reset_interrupt(enum gpio_signal signal)
{
}
static inline void chipset_ap_rst_interrupt(enum gpio_signal signal)
{
}
static inline void chipset_power_good_interrupt(enum gpio_signal signal)
{
}
static inline void chipset_watchdog_interrupt(enum gpio_signal signal)
{
}

#endif /* !CONFIG_AP_POWER_CONTROL */

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
 * GPIO interrupt handler of AP_RST_L signal from PMIC.
 * PMIC uses this signal to notify AP reset.
 *
 * It is used in Qualcomm chipset power sequence.
 */
void chipset_ap_rst_interrupt(enum gpio_signal signal);

/**
 * GPIO interrupt handler of warm reset signal from servo or H1.
 *
 * It is used in Qualcomm/MediaTek chipset power sequence.
 */
void chipset_warm_reset_interrupt(enum gpio_signal signal);

/**
 * GPIO interrupt handler of the power good signal (pull rail of warm reset).
 *
 * It is used in Qualcomm chipset power sequence.
 */
void chipset_power_good_interrupt(enum gpio_signal signal);

/**
 * GPIO interrupt handler of watchdog from AP.
 *
 * It is used in MT8183 chipset, where it must be setup to trigger on falling
 * edge only.
 */
void chipset_watchdog_interrupt(enum gpio_signal signal);

/**
 * Callback which allows board to take custom action on G3 timer expiration
 *
 * @param last_shutdown_time Last shutdown time
 * @param target             Expiration time. Can be modified by board.
 * @param now                Current time
 * @return Action to take
 */
__override_proto enum critical_shutdown
board_system_is_idle(uint64_t last_shutdown_time, uint64_t *target,
		     uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CHIPSET_H */
