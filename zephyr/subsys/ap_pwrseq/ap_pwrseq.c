/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_pwrseq_drv_sm.h"
#include "task.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#define AP_PWRSEQ_EVENT_MASK GENMASK(AP_PWRSEQ_EVENT_COUNT - 1, 0)

struct ap_pwrseq_data {
	/* State machine data reference. */
	void *sm_data;
	/* Driver event object to receive events posted. */
	struct k_event evt;
	/*
	 * This mutex object blocks state machine transitions to prevent race
	 * condition when doing power state related tasks. Must be held when
	 * accessing `sm_data`.
	 */
	struct k_mutex mux;
};

/* Resolve into substate name string */
#define AP_PWRSEQ_SUB_STATE_STR_DEFINE_WITH_COMA(state) state,

#define AP_PWRSEQ_EACH_SUB_STATE_STR_DEFINE(node_id, prop, idx) \
	AP_PWRSEQ_SUB_STATE_STR_DEFINE_WITH_COMA(               \
		DT_CAT5(node_id, _P_, prop, _IDX_, idx))

#define AP_PWRSEQ_EACH_SUB_STATE_STR_DEF_NODE_CHILD_DEFINE(node_id)          \
	COND_CODE_1(                                                         \
		DT_NODE_HAS_PROP(node_id, application),                      \
		(DT_FOREACH_PROP_ELEM(node_id, application,                  \
				      AP_PWRSEQ_EACH_SUB_STATE_STR_DEFINE)), \
		(COND_CODE_1(DT_NODE_HAS_PROP(node_id, chipset),             \
			     (DT_FOREACH_PROP_ELEM(                          \
				     node_id, chipset,                       \
				     AP_PWRSEQ_EACH_SUB_STATE_STR_DEFINE)),  \
			     ())))

static const char *const ap_pwrseq_state_str[AP_POWER_STATE_COUNT] = {
	"UNINIT",
	"G3",
	"S5",
	"S4",
	"S3",
	"S2",
	"S1",
	"S0",
	DT_FOREACH_STATUS_OKAY(
		ap_pwrseq_sub_states,
		AP_PWRSEQ_EACH_SUB_STATE_STR_DEF_NODE_CHILD_DEFINE)
};
BUILD_ASSERT(ARRAY_SIZE(ap_pwrseq_state_str) == AP_POWER_STATE_COUNT);

static struct ap_pwrseq_data ap_pwrseq_task_data;

static void ap_pwrseq_send_callback(const struct device *dev,
				    const enum ap_pwrseq_state entry,
				    const enum ap_pwrseq_state exit,
				    bool is_entry)
{
	const enum ap_pwrseq_state state = is_entry ? entry : exit;

	STRUCT_SECTION_FOREACH(ap_pwrseq_state_cb, cb)
	{
		if (cb->is_entry == is_entry &&
		    (cb->states_bit_mask & BIT(state))) {
			cb->cb(dev, entry, exit);
		}
	}
}

static void ap_pwrseq_send_entry_callback(const struct device *dev,
					  const enum ap_pwrseq_state entry,
					  const enum ap_pwrseq_state exit)
{
	ap_pwrseq_send_callback(dev, entry, exit, true);
}

static void ap_pwrseq_send_exit_callback(const struct device *dev,
					 const enum ap_pwrseq_state entry,
					 const enum ap_pwrseq_state exit)
{
	ap_pwrseq_send_callback(dev, entry, exit, false);
}

static uint32_t ap_pwrseq_wait_event(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;
	uint32_t events;

	events = k_event_wait(&data->evt, AP_PWRSEQ_EVENT_MASK, false,
			      Z_FOREVER);
	/* Clear all events posted */
	k_event_clear(&data->evt, events);

	return events & AP_PWRSEQ_EVENT_MASK;
}

static void ap_pwrseq_thread(void *arg, void *unused1, void *unused2)
{
	struct device *const dev = (struct device *)arg;
	struct ap_pwrseq_data *const data = dev->data;
	enum ap_pwrseq_state cur_state, new_state;
	int run_status;
	uint32_t events;

	LOG_INF("Power Sequence thread start");
	while (true) {
		events = ap_pwrseq_wait_event(dev);
		if (!events) {
			continue;
		}
		LOG_DBG("Events posted: %0#x", events);

		/**
		 * Process generated events and keep looping while state
		 * transitions are occurring.
		 **/
		while (true) {
			ap_pwrseq_state_lock(dev);

			cur_state = ap_pwrseq_sm_get_cur_state(data->sm_data);
			run_status =
				ap_pwrseq_sm_run_state(data->sm_data, events);
			new_state = ap_pwrseq_sm_get_cur_state(data->sm_data);

			ap_pwrseq_state_unlock(dev);
			if (run_status) {
				/* Was this terminated? */
				return;
			}

			/* Check if state transition took place */
			if (cur_state == new_state) {
				break;
			}
			LOG_INF("Power state: %s -> %s",
				ap_pwrseq_get_state_str(cur_state),
				ap_pwrseq_get_state_str(new_state));

			ap_pwrseq_send_exit_callback(dev, new_state, cur_state);

			ap_pwrseq_send_entry_callback(dev, new_state,
						      cur_state);
			LOG_INF("power state %d = %s,", new_state,
				ap_pwrseq_get_state_str(new_state));
		}
	}
}

static int ap_pwrseq_driver_init(const struct device *dev);

DEVICE_DEFINE(ap_pwrseq_dev, "ap_pwrseq_drv", ap_pwrseq_driver_init, NULL,
	      &ap_pwrseq_task_data, NULL, POST_KERNEL,
	      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

BUILD_ASSERT(
	EC_TASK_PRIORITY(EC_TASK_AP_PWRSEQ_PRIO) ==
		CONFIG_AP_PWRSEQ_THREAD_PRIORITY,
	"EC_TASK_AP_PWRSEQ_PRIO does not match CONFIG_AP_PWRSEQ_THREAD_PRIORITY.");

K_THREAD_DEFINE(ap_pwrseq_tid, CONFIG_AP_PWRSEQ_STACK_SIZE, ap_pwrseq_thread,
		DEVICE_GET(ap_pwrseq_dev), NULL, NULL,
		CONFIG_AP_PWRSEQ_THREAD_PRIORITY, 0, SYS_FOREVER_MS);

static int ap_pwrseq_driver_init(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;

	data->sm_data = ap_pwrseq_sm_get_instance();

	k_mutex_init(&data->mux);
	k_event_init(&data->evt);

	return 0;
}

k_tid_t get_ap_pwrseq_thread(void)
{
	return ap_pwrseq_tid;
}

/**
 *  Global functions definition.
 **/
const struct device *ap_pwrseq_get_instance(void)
{
	return DEVICE_GET(ap_pwrseq_dev);
}

int ap_pwrseq_start(const struct device *dev, enum ap_pwrseq_state init_state)
{
	struct ap_pwrseq_data *const data = dev->data;
	int ret;

	ap_pwrseq_state_lock(dev);
	ret = ap_pwrseq_sm_init(data->sm_data, ap_pwrseq_tid, init_state);
	ap_pwrseq_state_unlock(dev);
	if (ret) {
		return ret;
	}

	k_thread_start(ap_pwrseq_tid);

	return 0;
}

void ap_pwrseq_post_event(const struct device *dev, enum ap_pwrseq_event event)
{
	struct ap_pwrseq_data *const data = dev->data;

	if (event >= AP_PWRSEQ_EVENT_COUNT) {
		return;
	}

	LOG_DBG("Posting Event: %0#lx", BIT(event));
	k_event_post(&data->evt, BIT(event));
}

enum ap_pwrseq_state ap_pwrseq_get_current_state(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;
	enum ap_pwrseq_state ret_state;

	ap_pwrseq_state_lock(dev);

	ret_state = ap_pwrseq_sm_get_cur_state(data->sm_data);

	ap_pwrseq_state_unlock(dev);

	return ret_state;
}

const char *const ap_pwrseq_get_state_str(enum ap_pwrseq_state state)
{
	if (state >= AP_POWER_STATE_COUNT) {
		return NULL;
	}

	return ap_pwrseq_state_str[state];
}

int ap_pwrseq_state_lock(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;

	/* Acquire lock to ensure no `run` operation is in progress. */
	return k_mutex_lock(&data->mux, K_FOREVER);
}

int ap_pwrseq_state_unlock(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;

	return k_mutex_unlock(&data->mux);
}
