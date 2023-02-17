/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _AP_PWRSEQ_H_
#define _AP_PWRSEQ_H_
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/** Starts the AP power sequence thread */
void ap_pwrseq_task_start(void);

void ap_pwrseq_wake(void);

#ifdef __cplusplus
extern "C" {
#endif

#define AP_POWER_SUB_STATE_ENUM_DEF_WITH_COMMA(node_id, prop, idx) \
	DT_CAT6(node_id, _P_, prop, _IDX_, idx, _STRING_UPPER_TOKEN),

#define AP_PWRSEQ_EACH_SUB_STATE_ENUM_DEF(node_id)                              \
	COND_CODE_1(                                                            \
		DT_NODE_HAS_PROP(node_id, chipset),                             \
		(DT_FOREACH_PROP_ELEM(node_id, chipset,                         \
				      AP_POWER_SUB_STATE_ENUM_DEF_WITH_COMMA)), \
		(COND_CODE_1(DT_NODE_HAS_PROP(node_id, application),            \
			     (DT_FOREACH_PROP_ELEM(                             \
				     node_id, application,                      \
				     AP_POWER_SUB_STATE_ENUM_DEF_WITH_COMMA)),  \
			     ())))

/** @brief AP power sequence valid power states. */
/* clang-format off */
enum ap_pwrseq_state {
	AP_POWER_STATE_UNINIT, /* EC and AP are Uninitialized */
	AP_POWER_STATE_G3, /* AP is OFF */
	AP_POWER_STATE_S5, /* AP is on soft off state */
	AP_POWER_STATE_S4, /* AP is suspended to Non-volatile disk */
	AP_POWER_STATE_S3, /* AP is suspended to RAM */
	AP_POWER_STATE_S2, /* AP is low wake-latency sleep */
	AP_POWER_STATE_S1, /* AP is in suspend state */
	AP_POWER_STATE_S0, /* AP is in active state */
	DT_FOREACH_STATUS_OKAY(ap_pwrseq_sub_states,
			       AP_PWRSEQ_EACH_SUB_STATE_ENUM_DEF)
	AP_POWER_STATE_COUNT,
	AP_POWER_STATE_UNDEF = 0xFFFE,
	AP_POWER_STATE_ERROR = 0xFFFF,
};
/* clang-format on */

/** @brief AP power sequence events. */
enum ap_pwrseq_event {
	AP_PWRSEQ_EVENT_POWER_STARTUP,
	AP_PWRSEQ_EVENT_POWER_SIGNAL,
	AP_PWRSEQ_EVENT_POWER_TIMEOUT,
	AP_PWRSEQ_EVENT_POWER_SHUTDOWN,
	AP_PWRSEQ_EVENT_HOST,
	AP_PWRSEQ_EVENT_COUNT,
};

/** @brief The signature for callback notification from AP power seqeuce driver.
 *
 * This function will be invoked by AP power sequence driver as configured by
 * functions `ap_pwrseq_register_state_entry_callback` or
 * `ap_pwrseq_register_state_entry_callback` for power state transitions.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @param entry Entering state in transition.
 *
 * @param exit Exiting state in transition.
 *
 * @retval None.
 */
typedef void (*ap_pwrseq_callback)(const struct device *dev,
				   enum ap_pwrseq_state entry,
				   enum ap_pwrseq_state exit);

struct ap_pwrseq_state_callback {
	/* Node used to link notifications. This is for internal use only */
	sys_snode_t node;
	/**
	 * Callback function, this will be invoked when AP power sequence
	 * enters or exits states selected by `states_bit_mask`.
	 **/
	ap_pwrseq_callback cb;
	/* Bitfield of states to invoke callback */
	uint32_t states_bit_mask;
};

/**
 * @brief Get AP power sequence device driver pointer.
 *
 * @param None.
 *
 * @retval AP power sequence device driver pointer.
 **/
const struct device *ap_pwrseq_get_instance(void);

/**
 * @brief Starts AP power sequence driver thread execution.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @param init_state state that will be executed when staring.
 *
 * @retval SUCCESS Driver starts execution.
 * @retval -EINVAL State provided is invalid.
 * @retval -EPERM  Driver is already started.
 **/
int ap_pwrseq_start(const struct device *dev, enum ap_pwrseq_state init_state);

/**
 * @brief Post event for AP power sequence driver.
 *
 * State machine is executed within AP power sequence thread, this thread goes
 * to sleep when state machine is idle and state transition is completed.
 * Events are posted to wake up AP power sequence thread and made available to
 * state machine only for the following iteration.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @param event Event posted to AP power seuqence driver.
 *
 * @retval None.
 **/
void ap_pwrseq_post_event(const struct device *dev, enum ap_pwrseq_event event);

/**
 * @brief Get enumeration value of current state of AP power sequence driver.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @retval Valid state enumeration value.
 * @retval AP_POWER_STATE_UNDEF if error.
 **/
enum ap_pwrseq_state ap_pwrseq_get_current_state(const struct device *dev);

/**
 * @brief Get null terminated string of selected state.
 *
 * @param state AP power sequence valid state.
 *
 * @retval String showing selected state name.
 * @retval NULL if state is invalid.
 **/
const char *const ap_pwrseq_get_state_str(enum ap_pwrseq_state state);

/**
 * @brief Lock current AP power sequence state.
 *
 * Once state machine is locked, it will not change its state until unlocked.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @retval SUCCESS Driver has been successfully locked, non-zero otherwise.
 **/
int ap_pwrseq_state_lock(const struct device *dev);

/**
 * @brief Unlock AP power sequence state.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @retval SUCCESS Driver has been successfully unlocked, non-zero otherwise.
 **/
int ap_pwrseq_state_unlock(const struct device *dev);

/**
 * @brief Register callback into AP power sequence driver.
 *
 * Callback function will be called by AP power sequence driver when entering
 * into selected states.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @param state_cb Pointer of `ap_pwrseq_state_callback` structure.
 *
 * @retval SUCCESS Callback was successfully registered.
 * @retval -EINVAL On error.
 **/
int ap_pwrseq_register_state_entry_callback(
	const struct device *dev, struct ap_pwrseq_state_callback *state_cb);

/**
 * @brief Register callback into AP power sequence driver.
 *
 * Callback function will be called by AP power sequence driver when exiting
 * from selected states.
 *
 * @param dev Pointer of AP power sequence device driver.
 *
 * @param state_cb Pointer of `ap_pwrseq_state_callback` structure.
 *
 * @retval SUCCESS Callback was successfully registered.
 * @retval -EINVAL On error.
 **/
int ap_pwrseq_register_state_exit_callback(
	const struct device *dev, struct ap_pwrseq_state_callback *state_cb);

#ifdef __cplusplus
}
#endif
#endif /* _AP_PWRSEQ_H_ */
