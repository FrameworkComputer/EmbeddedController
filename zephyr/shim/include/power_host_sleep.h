/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __POWER_HOST_SLEEP_H
#define __POWER_HOST_SLEEP_H

/*
 * This file is for Zephyr ap_pwrseq to reuse legacy EC code.
 * Eventually this file should be removed.
 *
 * TODO: Declaration in this file should be removed once it can be replaced
 * by implementation in Zephyr code.
 */
#if CONFIG_AP_PWRSEQ

#include "ec_commands.h"
#include "host_command.h"
#include "lpc.h"
#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

/********************************************************************/
/* power.h */
enum power_state {
	/* Steady states */
	POWER_G3 = 0, /*
		       * System is off (not technically all the way into G3,
		       * which means totally unpowered...)
		       */
	POWER_S5, /* System is soft-off */
	POWER_S4, /* System is suspended to disk */
	POWER_S3, /* Suspend; RAM on, processor is asleep */
	POWER_S0, /* System is on */
#if CONFIG_AP_PWRSEQ_S0IX
	POWER_S0ix,
#endif
	/* Transitions */
	POWER_G3S5, /* G3 -> S5 (at system init time) */
	POWER_S5S3, /* S5 -> S3 (skips S4 on non-Intel systems) */
	POWER_S3S0, /* S3 -> S0 */
	POWER_S0S3, /* S0 -> S3 */
	POWER_S3S5, /* S3 -> S5 (skips S4 on non-Intel systems) */
	POWER_S5G3, /* S5 -> G3 */
	POWER_S3S4, /* S3 -> S4 */
	POWER_S4S3, /* S4 -> S3 */
	POWER_S4S5, /* S4 -> S5 */
	POWER_S5S4, /* S5 -> S4 */
#if CONFIG_AP_PWRSEQ_S0IX
	POWER_S0ixS0, /* S0ix -> S0 */
	POWER_S0S0ix, /* S0 -> S0ix */
#endif
};

#if CONFIG_AP_PWRSEQ_HOST_SLEEP
/* Context to pass to a host sleep command handler. */
struct host_sleep_event_context {
	uint32_t sleep_transitions; /* Number of sleep transitions observed */
	uint16_t sleep_timeout_ms; /* Timeout in milliseconds */
};

void ap_power_chipset_handle_host_sleep_event(
	enum host_sleep_event state, struct host_sleep_event_context *ctx);
void power_set_host_sleep_state(enum host_sleep_event state);
#endif /* CONFIG_AP_PWRSEQ_HOST_SLEEP */

#endif /* CONFIG_AP_PWRSEQ */

#ifdef __cplusplus
}
#endif

#endif /* __POWER_HOST_SLEEP_H */
