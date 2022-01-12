/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_COMMON_PWRSEQ_H__
#define __X86_COMMON_PWRSEQ_H__

#include <drivers/espi.h>
#include <drivers/gpio.h>
#include <logging/log.h>
#include <x86_power_signals.h>

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

/*
 * AP hard shutdowns are logged on the same path as resets.
 */
enum pwrseq_chipset_shutdown_reason {
	PWRSEQ_CHIPSET_SHUTDOWN_POWERFAIL,
	/* Forcing a shutdown as part of EC initialization */
	PWRSEQ_CHIPSET_SHUTDOWN_INIT,
	/* Forcing shutdown with command */
	PWRSEQ_CHIPSET_SHUTDOWN_CONSOLE_CMD,
	/* Forcing a shutdown to effect entry to G3. */
	PWRSEQ_CHIPSET_SHUTDOWN_G3,
	/* Force a chipset shutdown from the power button through EC */
	PWRSEQ_CHIPSET_SHUTDOWN_BUTTON,

	PWRSEQ_CHIPSET_SHUTDOWN_COUNT,
};

/* This encapsulates the attributes of the state machine */
struct pwrseq_context {
	/* On power-on start boot up sequence */
	enum power_states_ndsx power_state;
	/* Indicate should exit G3 power state or not */
	bool want_g3_exit;

	/*
	 * Current input power signal states. Each bit represents an input
	 * power signal that is defined by enum power_signal in same order.
	 * 1 - signal state is asserted.
	 * 0 - signal state is de-asserted.
	 */
	uint32_t in_signals;
	/* Input signal state we're waiting for */
	uint32_t in_want;
	/* Signal values which print debug output */
	uint32_t in_debug;
};
#endif /* __X86_COMMON_PWRSEQ_H__ */
