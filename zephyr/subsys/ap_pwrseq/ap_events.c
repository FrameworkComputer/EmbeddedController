/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

static sys_slist_t callbacks;
/*
 * Contains the accumulated set of events that any
 * callback has registered. This is a hint to detect events that
 * have never had any callbacks registered, in which case the
 * callback list does not have to be run.
 */
static uint32_t cb_set;

static int ap_power_ev_manage_callback(struct ap_power_ev_callback *cb,
				       bool set)
{
	__ASSERT(cb, "No callback!");
	__ASSERT(cb->handler, "No callback handler!");

	if (!sys_slist_is_empty(&callbacks)) {
		if (!sys_slist_find_and_remove(&callbacks, &cb->node)) {
			if (!set) {
				return -EINVAL;
			}
		}
	}
	if (set) {
		sys_slist_prepend(&callbacks, &cb->node);
		cb_set |= cb->events;
	}
	return 0;
}

int ap_power_ev_add_callback(struct ap_power_ev_callback *cb)
{
	return ap_power_ev_manage_callback(cb, true);
}

int ap_power_ev_remove_callback(struct ap_power_ev_callback *cb)
{
	return ap_power_ev_manage_callback(cb, false);
}

void ap_power_ev_add_events(struct ap_power_ev_callback *cb, uint32_t events)
{
	__ASSERT(cb, "Callback pointer should not be NULL");

	cb->events |= events;
	cb_set |= events;
}

/*
 * Run the callback list
 */
void ap_power_ev_send_callbacks(enum ap_power_events event)
{
	struct ap_power_ev_data data;
	struct ap_power_ev_callback *cb, *tmp;

	/*
	 * If no callbacks for this event, don't run the queue.
	 */
	if ((cb_set & event) == 0) {
		return;
	}
	data.event = event;
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&callbacks, cb, tmp, node)
	{
		if (cb->events & event) {
			cb->handler(cb, data);
		}
	}
}
