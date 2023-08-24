/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_pwrseq_sm.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

struct ap_pwrseq_sm_data {
	/* Zephyr SMF context */
	struct smf_ctx smf;
	/* Pointer to array of states structures */
	const struct ap_pwrseq_smf **states;
	/* Bitfiled of events */
	uint32_t events;
	/* Id of current thread executing state machine */
	k_tid_t tid;
	/* State entering during state transition */
	enum ap_pwrseq_state entry;
	/* State exiting during state transition */
	enum ap_pwrseq_state exit;
	/* Flag to inform if current `run` action has been handled */
	bool run_handled;
	/* Flag to inform if current `entry` action has been handled */
	bool entry_handled;
	/* Flag to inform if current `exit` action has been handled */
	bool exit_handled;
	/* Flag to inform that state transition is in progress */
	bool in_transition;
};

/**
 * Declare weak `struct smf_state` definitions of all ACPI states for
 * architecture and chipset level. These are used as placeholder to keep AP
 * power sequence state machine hierarchy in case corresponding state action
 * handlers are not provided by implementation.
 **/
#define AP_POWER_ARCH_STATE_WEAK_DEFINE(name)                 \
	const struct smf_state __weak arch_##name##_actions = \
		SMF_CREATE_STATE(NULL, NULL, NULL, NULL);

#define AP_POWER_CHIPSET_STATE_WEAK_DEFINE(name)                 \
	const struct smf_state __weak chipset_##name##_actions = \
		SMF_CREATE_STATE(NULL, NULL, NULL, &arch_##name##_actions);

/**
 * Declare weak `struct ap_pwrseq_smf` definitions of all ACPI states for
 * application level. These are used as placeholder to keep AP
 * power sequence state machine hierarchy in case corresponding state action
 * handlers are not provided by implementation.
 **/
#define AP_POWER_APP_STATE_WEAK_DEFINE(name)                            \
	const struct ap_pwrseq_smf __weak app_state_##name = {          \
		.actions = SMF_CREATE_STATE(NULL, NULL, NULL,           \
					    &chipset_##name##_actions), \
		.state = name                                           \
	};

#define AP_POWER_STATE_WEAK_DEFINE(name)         \
	AP_POWER_ARCH_STATE_WEAK_DEFINE(name)    \
	AP_POWER_CHIPSET_STATE_WEAK_DEFINE(name) \
	AP_POWER_APP_STATE_WEAK_DEFINE(name)

AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_G3)
AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_S5)
AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_S4)
AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_S3)
AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_S2)
AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_S1)
AP_POWER_STATE_WEAK_DEFINE(AP_POWER_STATE_S0)

#define AP_PWRSEQ_STATE_DEFINE(name) [name] = &app_state_##name

/* Sub States defines */
#define AP_PWRSEQ_APP_SUB_STATE_DEFINE(state) [state] = &app_state_##state,

#define AP_PWRSEQ_APP_SUB_STATE_DEFINE_(state) \
	AP_PWRSEQ_APP_SUB_STATE_DEFINE(state)

#define AP_PWRSEQ_EACH_APP_SUB_STATE_NODE_DEFINE__(node_id, prop, idx) \
	AP_PWRSEQ_APP_SUB_STATE_DEFINE_(                               \
		DT_CAT6(node_id, _P_, prop, _IDX_, idx, _STRING_UPPER_TOKEN))

#define AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_NODE_DEFINE(state) \
	[state] = &chipset_##state##_actions,

#define AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_NODE_DEFINE_(state) \
	AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_NODE_DEFINE(state)

#define AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_NODE_DEFINE__(node_id, prop, idx) \
	AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_NODE_DEFINE_(                     \
		DT_CAT6(node_id, _P_, prop, _IDX_, idx, _STRING_UPPER_TOKEN))

#define AP_PWRSEQ_EACH_SUB_STATE_NODE_CHILD_DEFINE(node_id)                   \
	COND_CODE_1(                                                          \
		DT_NODE_HAS_PROP(node_id, chipset),                           \
		(DT_FOREACH_PROP_ELEM(                                        \
			node_id, chipset,                                     \
			AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_NODE_DEFINE__)),     \
		(COND_CODE_1(                                                 \
			DT_NODE_HAS_PROP(node_id, application),               \
			(DT_FOREACH_PROP_ELEM(                                \
				node_id, application,                         \
				AP_PWRSEQ_EACH_APP_SUB_STATE_NODE_DEFINE__)), \
			())))

/**
 * @brief Array containing power state action handlers for all state and
 * and substates available for AP power sequence state machine, these items
 * correspond to `enum ap_pwrseq_state`.
 **/
static const struct ap_pwrseq_smf *ap_pwrseq_states[AP_POWER_STATE_COUNT] = {
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_G3),
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_S5),
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_S4),
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_S3),
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_S2),
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_S1),
	AP_PWRSEQ_STATE_DEFINE(AP_POWER_STATE_S0),
	DT_FOREACH_STATUS_OKAY(ap_pwrseq_sub_states,
			       AP_PWRSEQ_EACH_SUB_STATE_NODE_CHILD_DEFINE)
};

static struct ap_pwrseq_sm_data sm_data_0 = {
	.states = ap_pwrseq_states,
};

/* Private functions available only for AP Power Sequence subsystem driver. */
void *ap_pwrseq_sm_get_instance(void)
{
	return &sm_data_0;
}

int ap_pwrseq_sm_init(void *const data, k_tid_t tid,
		      enum ap_pwrseq_state init_state)
{
	struct ap_pwrseq_sm_data *sm_data = data;

	if (sm_data->smf.current || sm_data->tid) {
		return -EPERM;
	}

	if (init_state >= AP_POWER_STATE_COUNT) {
		return -EINVAL;
	}

	sm_data->entry = sm_data->exit = AP_POWER_STATE_UNDEF;
	smf_set_initial(&sm_data->smf,
			(const struct smf_state *)sm_data->states[init_state]);
	sm_data->tid = tid;

	return 0;
}

int ap_pwrseq_sm_run_state(void *const data, uint32_t events)
{
	struct ap_pwrseq_sm_data *sm_data = data;
	int ret;

	if (!IS_ENABLED(CONFIG_EMUL_AP_PWRSEQ_DRIVER) &&
	    sm_data->tid != k_current_get()) {
		/* Called by wrong thread */
		return -EPERM;
	}

	if (sm_data->smf.current == NULL) {
		return -EINVAL;
	}

	sm_data->in_transition = false;
	sm_data->entry = sm_data->exit = AP_POWER_STATE_UNDEF;
	sm_data->run_handled = false;
	sm_data->events = events;

	ret = smf_run_state((struct smf_ctx *const)sm_data);

	return ret;
}

enum ap_pwrseq_state ap_pwrseq_sm_get_cur_state(void *const data)
{
	struct ap_pwrseq_sm_data *sm_data = data;

	if (!sm_data->smf.current) {
		return AP_POWER_STATE_UNDEF;
	}

	return ((struct ap_pwrseq_smf *)sm_data->smf.current)->state;
}

/* Public functions for action handlers implementation. */
int ap_pwrseq_sm_set_state(void *const data, enum ap_pwrseq_state state)
{
	struct ap_pwrseq_sm_data *sm_data = data;

	if (!IS_ENABLED(CONFIG_EMUL_AP_PWRSEQ_DRIVER) &&
	    sm_data->tid != k_current_get()) {
		/* Called by wrong thread */
		return -EPERM;
	}

	if (state >= AP_POWER_STATE_COUNT ||
	    /* Only one state transition is permited within `run` iterations */
	    sm_data->in_transition) {
		return -EINVAL;
	}

	/* Transition has started, update corresponding flags */
	sm_data->in_transition = true;
	sm_data->entry_handled = sm_data->exit_handled = false;
	sm_data->entry = state;
	sm_data->exit = ((struct ap_pwrseq_smf *)sm_data->smf.current)->state;
	smf_set_state((struct smf_ctx *const)&sm_data->smf,
		      (const struct smf_state *)sm_data->states[state]);

	return 0;
}

bool ap_pwrseq_sm_is_event_set(void *const data, enum ap_pwrseq_event event)
{
	struct ap_pwrseq_sm_data *sm_data = data;

	return ((sm_data->events & BIT(event)) == BIT(event));
}

enum ap_pwrseq_state ap_pwrseq_sm_get_entry_state(void *const data)
{
	struct ap_pwrseq_sm_data *sm_data = data;

	if (!IS_ENABLED(CONFIG_EMUL_AP_PWRSEQ_DRIVER) &&
	    sm_data->tid != k_current_get()) {
		/* Called by wrong thread */
		return -EPERM;
	}

	return sm_data->entry;
}

enum ap_pwrseq_state ap_pwrseq_sm_get_exit_state(void *const data)
{
	struct ap_pwrseq_sm_data *sm_data = data;

	if (!IS_ENABLED(CONFIG_EMUL_AP_PWRSEQ_DRIVER) &&
	    sm_data->tid != k_current_get()) {
		/* Called by wrong thread */
		return -EPERM;
	}

	return sm_data->exit;
}

#define AP_POWER_SM_HANDLER_DEF(action)                                \
	void ap_pwrseq_sm_exec_##action##_handler(                     \
		void *const data, ap_pwr_state_action_handler handler) \
	{                                                              \
		struct ap_pwrseq_sm_data *sm_data = data;              \
		if (handler && !sm_data->action##_handled)             \
			sm_data->action##_handled = !!handler(data);   \
	}

AP_POWER_SM_HANDLER_DEF(entry)
AP_POWER_SM_HANDLER_DEF(run)
AP_POWER_SM_HANDLER_DEF(exit)
