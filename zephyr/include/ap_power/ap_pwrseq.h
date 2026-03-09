/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _AP_PWRSEQ_H_
#define _AP_PWRSEQ_H_
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifndef CONFIG_AP_PWRSEQ_DRIVER
void ap_pwrseq_wake(void);
#else

#ifdef __cplusplus
extern "C" {
#endif

#define AP_POWER_SUB_STATE_ENUM_DEF_WITH_COMMA_(id) DT_CAT(AP_POWER_STATE_, id),

#define AP_POWER_SUB_STATE_ENUM_DEF_WITH_COMMA(node_id, prop, idx) \
	AP_POWER_SUB_STATE_ENUM_DEF_WITH_COMMA_(                   \
		DT_CAT6(node_id, _P_, prop, _IDX_, idx, _STRING_TOKEN))

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

/**
 * @brief AP power sequence valid power states.
 *
 * Note: States enum list MUST remain arranged from the lowest to the highest
 * power state.
 *
 **/
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

/** @brief The signature for callback notification from AP power sequence
 * driver.
 *
 * This function will be invoked by AP power sequence driver when a power state
 * transition occurs, for all callbacks registered via
 * @ref AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE or
 * @ref AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE.
 *
 * @param dev Pointer of AP power sequence device driver.
 * @param entry Entering state in transition.
 * @param exit Exiting state in transition.
 */
typedef void (*ap_pwrseq_callback)(const struct device *dev,
				   enum ap_pwrseq_state entry,
				   enum ap_pwrseq_state exit);

/**
 * @brief AP power sequence state callback entry, placed in iterable section.
 *
 * Use @ref AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE_NAMED,
 * @ref AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE_NAMED, or their unnamed variants
 * to instantiate this struct — do not populate it directly.
 */
struct ap_pwrseq_state_cb {
	/** Callback function invoked on the matching state transition. */
	ap_pwrseq_callback cb;
	/** Bitmask of @ref ap_pwrseq_state values that trigger this callback.
	 */
	uint32_t states_bit_mask;
	/** true = entry callback, false = exit callback. */
	bool is_entry;
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
 * @brief Register an AP power sequence state entry callback with a custom name.
 *
 * Same as @ref AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE but allows specifying a
 * custom @p name for the callback structure. Useful when multiple callbacks use
 * the same function pointer.
 *
 * @param name        Unique name for the callback structure variable.
 * @param cb_fn       Callback function of type @ref ap_pwrseq_callback.
 * @param ...         One or more @ref ap_pwrseq_state values that trigger the
 *                    callback on state entry. Each state is converted to a
 *                    bitmask via BIT() and OR-ed together.
 */
#define AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE_NAMED(name, cb_fn, ...)         \
	static const STRUCT_SECTION_ITERABLE(ap_pwrseq_state_cb,              \
					     _ap_pwrseq_entry_cb__##name) = { \
		.cb = (cb_fn),                                                \
		.states_bit_mask = (FOR_EACH(BIT, (|), __VA_ARGS__)),         \
		.is_entry = true,                                             \
	}

/**
 * @brief Register an AP power sequence state exit callback with a custom name.
 *
 * Same as @ref AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE but allows specifying a
 * custom @p name for the callback structure. Useful when multiple callbacks use
 * the same function pointer.
 *
 * @param name        Unique name for the callback structure variable.
 * @param cb_fn       Callback function of type @ref ap_pwrseq_callback.
 * @param ...         One or more @ref ap_pwrseq_state values that trigger the
 *                    callback on state exit. Each state is converted to a
 *                    bitmask via BIT() and OR-ed together.
 */
#define AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE_NAMED(name, cb_fn, ...)         \
	static const STRUCT_SECTION_ITERABLE(ap_pwrseq_state_cb,             \
					     _ap_pwrseq_exit_cb__##name) = { \
		.cb = (cb_fn),                                               \
		.states_bit_mask = (FOR_EACH(BIT, (|), __VA_ARGS__)),        \
		.is_entry = false,                                           \
	}

/**
 * @brief Register an AP power sequence state entry callback.
 *
 * Statically registers a callback invoked whenever the AP power sequence
 * driver enters any of the states in @p states_mask. The callback structure
 * is placed in a linker iterable section; no runtime registration call is
 * needed.
 *
 * @param cb_fn       Callback function of type @ref ap_pwrseq_callback.
 *                    Also used as the unique name for the callback structure.
 * @param ...         One or more @ref ap_pwrseq_state values that trigger the
 *                    callback on state entry. Each state is converted to a
 *                    bitmask via BIT() and OR-ed together.
 */
#define AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE(cb_fn, ...) \
	AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE_NAMED(cb_fn, cb_fn, __VA_ARGS__)

/**
 * @brief Register an AP power sequence state exit callback.
 *
 * Statically registers a callback invoked whenever the AP power sequence
 * driver exits any of the states in @p states_mask. The callback structure
 * is placed in a linker iterable section; no runtime registration call is
 * needed.
 *
 * @param cb_fn       Callback function of type @ref ap_pwrseq_callback.
 *                    Also used as the unique name for the callback structure.
 * @param ...         One or more @ref ap_pwrseq_state values that trigger the
 *                    callback on state exit. Each state is converted to a
 *                    bitmask via BIT() and OR-ed together.
 */
#define AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE(cb_fn, ...) \
	AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE_NAMED(cb_fn, cb_fn, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* CONFIG_AP_PWRSEQ_DRIVER */
#endif /* _AP_PWRSEQ_H_ */
