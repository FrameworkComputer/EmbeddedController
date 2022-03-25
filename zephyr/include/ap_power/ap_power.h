/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Public APIs for AP power sequence.
 *
 * Defines the API for AP event notification,
 * the API to register and receive notification callbacks when
 * application processor (AP) events happen
 */

#ifndef __AP_POWER_AP_POWER_H__
#define __AP_POWER_AP_POWER_H__

#include <kernel.h>

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
	AP_POWER_STARTUP = BIT(1),
	AP_POWER_RESUME = BIT(2),
	AP_POWER_SUSPEND = BIT(3),
	AP_POWER_RESUME_INIT = BIT(4),
	AP_POWER_SUSPEND_COMPLETE = BIT(5),
	AP_POWER_SHUTDOWN = BIT(6),
	AP_POWER_SHUTDOWN_COMPLETE = BIT(7),
	AP_POWER_HARD_OFF = BIT(8),
	AP_POWER_RESET = BIT(9),
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
 * ap_power_ev_init_callback can be used to initialise this structure.
 */
struct ap_power_ev_callback {
	sys_snode_t node;	/* Only usable by AP power event code */
	ap_power_ev_callback_handler_t handler;
	enum ap_power_events events;	/* Events to listen for */
};
/** @endcond */

/**
 * @brief Initialise a struct ap_power_ev_callback properly.
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @param handler The function pointer to call.
 * @param events The bitmask of events to be called for.
 */
static inline void ap_power_ev_init_callback(struct ap_power_ev_callback *cb,
				ap_power_ev_callback_handler_t handler,
				enum ap_power_events events)
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
void ap_power_ev_add_events(struct ap_power_ev_callback *cb,
			    enum ap_power_events events);

/**
 * @brief Update a callback event mask to remove events
 *
 * @param callback A valid ap_power_ev_callback structure pointer.
 * @param events The bitmask of events to remove.
 */
static inline void ap_power_ev_remove_events(struct ap_power_ev_callback *cb,
					     enum ap_power_events events)
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

#endif /* __AP_POWER_AP_POWER_H__ */
