/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__
#define __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__

#include <init.h>
#include <kernel.h>
#include <zephyr/types.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power_host_sleep.h>
#include <x86_common_pwrseq.h>

#define DT_DRV_COMPAT	intel_ap_pwrseq

/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_MS 250

enum power_states_ndsx chipset_pwr_sm_run(enum power_states_ndsx curr_state);
void init_chipset_pwr_seq_state(void);
enum power_states_ndsx chipset_pwr_seq_get_state(void);
void request_exit_hardoff(bool should_exit);
enum power_states_ndsx pwr_sm_get_state(void);
const char * const pwr_sm_get_state_name(enum power_states_ndsx state);
void apshutdown(void);
void ap_pwrseq_handle_chipset_reset(void);

#endif /* __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__ */
