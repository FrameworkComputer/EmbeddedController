/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PECI based cpu power governer
 */

#ifndef __CROS_EC_CPU_POWER_H
#define __CROS_EC_CPU_POWER_H

#ifdef CONFIG_CHIPSET_AMD
#include "amd_cpu_power_interface.h"
#elif CONFIG_CHIPSET_INTEL
#include "intel_cpu_power_interface.h"
#endif

enum clear_reasons {
	PROCHOT_CLEAR_REASON_SUCCESS,
	PROCHOT_CLEAR_REASON_NOT_POWER,
	PROCHOT_CLEAR_REASON_FORCE,
};

void update_soc_power_limit(bool force_update, bool force_no_adapter);

void update_chipset_ready(int status);

#ifdef CONFIG_PD_CCG8_EPR
void update_cpu_power_limit_events(uint8_t pd_event, int enable);

/**
 * Clear the PROCHOT after the power limit update is complete.
 *
 * @param reason The reason to clear the PROCHOT
 */
void power_limit_clear_prochot(enum clear_reasons reason);

/**
 * Check the power limit pending events. If there is an event
 * the EC should update the power limit value.
 *
 * @return power limit pending events
 */
uint8_t power_limit_get_events(void);
#endif

#endif	/* __CROS_EC_CPU_POWER_H */
