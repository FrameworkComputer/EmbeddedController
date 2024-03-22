/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__
#define __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

#ifdef CONFIG_AP_PWRSEQ_DRIVE
#include <ap_power/ap_pwrseq.h>
#else
#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#endif
#include <ap_power_host_sleep.h>
#include <x86_common_pwrseq.h>

#ifndef CONFIG_AP_PWRSEQ_DRIVER
/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_MS 250

enum power_states_ndsx chipset_pwr_sm_run(enum power_states_ndsx curr_state);
enum power_states_ndsx chipset_pwr_seq_get_state(void);
enum power_states_ndsx pwr_sm_get_state(void);
const char *const pwr_sm_get_state_name(enum power_states_ndsx state);
#else
enum ap_pwrseq_state chipset_pwr_seq_get_state(void);
const char *const pwr_sm_get_state_name(enum ap_pwrseq_state state);
#endif
void request_start_from_g3(void);
void apshutdown(void);
void ap_pwrseq_handle_chipset_reset(void);
void set_start_from_g3_delay_seconds(uint32_t d_time);

/**
 * @brief Check if primary AP power rail is good.
 *
 * @return true if primary AP power is good, and false otherwise.
 */
bool chipset_is_prim_power_good(void);

/**
 * @brief Check if AP power state is good to have operational Virtual Wire (VW)
 * interface.
 *
 * @return true if AP Power state is good for VW, and false otherwise.
 */
bool chipset_is_vw_power_good(void);

/**
 * @brief Check if all AP power rails are good.
 *
 * @return true if all AP power rails are good, and false otherwise.
 */
bool chipset_is_all_power_good(void);
#endif /* __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__ */
