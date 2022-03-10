/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset interface APIs */

#include "common.h"

#if defined(CONFIG_AP_PWRSEQ)
#include "ap_power/ap_power_interface.h"
#include "chipset_state_check.h"

int chipset_in_state(int state_mask)
{
	return ap_power_in_state(state_mask);
}

int chipset_in_or_transitioning_to_state(int state_mask)
{
	return ap_power_in_or_transitioning_to_state(state_mask);
}

void chipset_exit_hard_off(void)
{
	ap_power_exit_hardoff();
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	ap_power_force_shutdown((enum ap_power_shutdown_reason)reason);
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	ap_power_reset((enum ap_power_shutdown_reason)reason);
}

/* TODO: b/214509787
 * To be added later when this functionality is implemented in ap_pwrseq.
 */
void chipset_throttle_cpu(int throttle) { }

void init_reset_log(void)
{
	ap_power_init_reset_log();
}

#else

#if !defined(HAS_TASK_CHIPSET)
#include "chipset.h"

/* When no chipset is present, assume it is always off. */
int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}
int chipset_in_or_transitioning_to_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ANY_OFF;
}
void chipset_exit_hard_off(void) { }
void chipset_throttle_cpu(int throttle) { }
void chipset_force_shutdown(enum chipset_shutdown_reason reason) { }
void chipset_reset(enum chipset_shutdown_reason reason) { }
void power_interrupt(enum gpio_signal signal) { }
void chipset_handle_espi_reset_assert(void) { }
void chipset_handle_reboot(void) { }
void chipset_reset_request_interrupt(enum gpio_signal signal) { }
void chipset_warm_reset_interrupt(enum gpio_signal signal) { }
void chipset_ap_rst_interrupt(enum gpio_signal signal) { }
void chipset_power_good_interrupt(enum gpio_signal signal) { }
void chipset_watchdog_interrupt(enum gpio_signal signal) { }
void init_reset_log(void) { }

#endif /* !defined(HAS_TASK_CHIPSET) */
#endif /*defined(CONFIG_AP_PWRSEQ) */
