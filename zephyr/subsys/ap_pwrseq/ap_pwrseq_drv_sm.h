/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _AP_PWRSEQ_INT_SM_H_
#define _AP_PWRSEQ_INT_SM_H_
#include "ap_power/ap_pwrseq.h"

#include <zephyr/kernel/thread.h>

/**
 * This following AP Power Sequence state machine functions are only available
 * for subsystem driver.
 **/

/**
 * @brief Obtain AP power sequence state machine instance.
 *
 * @param None.
 *
 * @retval Return instance data of the state machine, only one instance is
 * allowed per application.
 **/
void *ap_pwrseq_sm_get_instance(void);

/**
 * @brief Sets AP power sequence state machine initial state.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @param tid AP power sequence instance thread associated to this state
 * machine. Functions `ap_pwrseq_sm_set_state` and `ap_pwrseq_sm_run_state` are
 * meant to be executed only within this thread context.
 *
 * @param init_state State machine initial state.
 *
 * @retval SUCCESS Upon success, init_state ‘entry’ action handlers on all
 * implemented levels will be invoked.
 * @retval -EINVAL State provided is invalid.
 * @retval -EPERM  State machine is already initialized.
 **/
int ap_pwrseq_sm_init(void *const data, k_tid_t tid,
		      enum ap_pwrseq_state init_state);

/**
 * @brief Execute current state `run` action handlers.
 *
 * This function is meant to be executed only within AP power sequence driver
 * thread context. `tid` was given in `ap_pwrseq_sm_init`.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @param events Events to be processed in current `run` iteration.
 *
 * @retval SUCCESS Upon success, provided `run` action handlers will be executed
 * for all levels in current state.
 * @retval -EINVAL State machine has not been initialized.
 **/
int ap_pwrseq_sm_run_state(void *const data, uint32_t events);

/**
 * @brief Get current state enumeration value.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @retval Enum value Upon success.
 * @retval AP_POWER_STATE_UNDEF If state machine has not been initialized.
 **/
enum ap_pwrseq_state ap_pwrseq_sm_get_cur_state(void *const data);
#endif /* _AP_PWRSEQ_INT_SM_H_ */
