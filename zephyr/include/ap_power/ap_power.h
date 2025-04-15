/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Public APIs for AP power sequence.
 *
 * Defines the API for AP event notification,
 * the API to register and receive notification callbacks when
 * application processor (AP) events happen.
 *
 * When the Zephyr based AP power sequence config is enabled,
 * the callbacks are almost all invoked within the context
 * of the power sequence task, so the state is stable
 * during the callback. The only exception to this is AP_POWER_RESET, which is
 * invoked as a result of receiving a PLTRST# virtual wire signal (if enabled).
 *
 * When the legacy power sequence config is enabled, the callbacks are invoked
 * from the HOOK_CHIPSET notifications.
 */

#ifndef __AP_POWER_AP_POWER_H__
#define __AP_POWER_AP_POWER_H__

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AP power events for callback notification.
 */
enum ap_power_events {
	/**
	 * Transitioning from hard-off to soft-off.
	 *
	 * On x86 this is the transition up from G3 to S5.
	 */
	AP_POWER_PRE_INIT = BIT(0),
	/**
	 * Transitioning from soft-off to suspend.
	 *
	 * On x86 this is going from S5 to S3.
	 */
	AP_POWER_STARTUP = BIT(1),
	/**
	 * Transitioning from suspend to active.
	 *
	 * This event is emitted on all suspend-active transitions, regardless
	 * of suspend level. In particular, on x86 it is triggered by transition
	 * from either of S3 or S0ix to S0.
	 */
	AP_POWER_RESUME = BIT(2),
	/**
	 * Transitioning from active to suspend.
	 *
	 * This is the opposite of AP_POWER_RESUME. On x86, it is emitted when
	 * leaving S0 to either of S3 or S0ix.
	 */
	AP_POWER_SUSPEND = BIT(3),
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
	/**
	 * Early transition from suspend to active.
	 *
	 * This event runs under the same conditions as AP_POWER_RESUME, but
	 * is guaranteed to run before AP_POWER_RESUME.
	 */
	AP_POWER_RESUME_INIT = BIT(4),
	/**
	 * Late transition from active to suspend.
	 *
	 * This event runs under the same conditions as AP_POWER_SUSPEND, but
	 * is guaranteed to run after AP_POWER_SUSPEND.
	 */
	AP_POWER_SUSPEND_COMPLETE = BIT(5),
#endif
	/**
	 * Transitioning from suspend to soft-off.
	 *
	 * This is the opposite of AP_POWER_STARTUP. On x86 it is the transition
	 * from S3 to S5.
	 */
	AP_POWER_SHUTDOWN = BIT(6),
	/**
	 * Late transition from suspend to soft-off.
	 *
	 * This runs under the same conditions as AP_POWER_SHUTDOWN, but runs
	 * after AP_POWER_SHUTDOWN.
	 */
	AP_POWER_SHUTDOWN_COMPLETE = BIT(7),
	/**
	 * Transitioning from soft-off to hard-off.
	 *
	 * This is the opposite of AP_POWER_PRE_INIT. On x86 it is the
	 * transition from S5 to G3.
	 */
	AP_POWER_HARD_OFF = BIT(8),
	/** Software reset occurred */
	AP_POWER_RESET = BIT(9),
	/**
	 * AP power state is now known.
	 *
	 * Prior to this event, the state of the AP is unknown
	 * and invalid. When this event is sent, the state is known
	 * and can be queried. Used by clients when their
	 * initialization depends upon the initial state of the AP.
	 */
	AP_POWER_INITIALIZED = BIT(10),

	/**
	 * S0ix suspend starts.
	 */
	AP_POWER_S0IX_SUSPEND_START = BIT(11),
	/**
	 * Transitioning from s0 to s0ix.
	 */
	AP_POWER_S0IX_SUSPEND = BIT(12),
	/**
	 * Transitioning from s0ix to s0.
	 */
	AP_POWER_S0IX_RESUME = BIT(13),
	/**
	 * si0x resume complete.
	 */
	AP_POWER_S0IX_RESUME_COMPLETE = BIT(14),
	/**
	 * Reset s0ix tracking.
	 */
	AP_POWER_S0IX_RESET_TRACKING = BIT(15),
};

/**
 * @brief AP data for callback argument.
 */
struct ap_power_ev_data {
	enum ap_power_events event;
	/* May need more data here */
};

struct ap_power_ev_callback;

/**
 * @brief Callback handler definition
 */
typedef void (*ap_power_ev_callback_handler_t)(struct ap_power_ev_callback *cb,
					       struct ap_power_ev_data data);

/**
 * @cond INTERNAL_HIDDEN
 *
 * Register a callback for the AP power events requested.
 * As many callbacks as needed can be added as long as each of them
 * are unique pointers of struct ap_power_ev_callback.
 * The storage must be static.
 *
 * ap_power_ev_init_callback can be used to initialize this structure.
 */
struct ap_power_ev_callback {
	sys_snode_t node; /* Only usable by AP power event code */
	ap_power_ev_callback_handler_t handler;
	uint32_t events; /* Events to listen for */
};
/** @endcond */

/**
 * @brief Initialise a struct ap_power_ev_callback properly.
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @param handler The function pointer to call.
 * @param events The bitmask of events to be called for.
 */
static inline void
ap_power_ev_init_callback(struct ap_power_ev_callback *cb,
			  ap_power_ev_callback_handler_t handler,
			  uint32_t events)
{
	__ASSERT(cb, "Callback pointer should not be NULL");
	__ASSERT(handler, "Callback handler pointer should not be NULL");

	cb->handler = handler;
	cb->events = events;
}

/**
 * @brief Update a callback event mask to listen for new events
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @param events The bitmask of events to add.
 */
void ap_power_ev_add_events(struct ap_power_ev_callback *cb, uint32_t events);

/**
 * @brief Update a callback event mask to remove events
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @param events The bitmask of events to remove.
 */
static inline void ap_power_ev_remove_events(struct ap_power_ev_callback *cb,
					     uint32_t events)
{
	__ASSERT(cb, "Callback pointer should not be NULL");

	cb->events &= ~events;
}

/**
 * @brief Add an AP event callback.
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @return 0 on success, negative errno on failure.
 */
int ap_power_ev_add_callback(struct ap_power_ev_callback *cb);

/**
 * @brief Remove an AP event callback.
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @return 0 on success, negative errno on failure.
 */
int ap_power_ev_remove_callback(struct ap_power_ev_callback *cb);

#ifdef __cplusplus
}
#endif

#endif /* __AP_POWER_AP_POWER_H__ */
