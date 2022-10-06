/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "task.h"
#include "util.h"

#include "mock/power.h"

LOG_MODULE_REGISTER(mock_power);

/* Mocks for ec/power/common.c and board specific implementations */
DEFINE_FAKE_VALUE_FUNC(enum power_state, power_handle_state, enum power_state);
DEFINE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DEFINE_FAKE_VOID_FUNC(chipset_power_on);
DEFINE_FAKE_VALUE_FUNC(int, command_power, int, const char **);

#define MOCK_POWER_LIST(FAKE)                 \
	{                                     \
		FAKE(power_handle_state);     \
		FAKE(chipset_force_shutdown); \
		FAKE(chipset_power_on);       \
		FAKE(command_power);          \
	}

/**
 * @brief Reset all the fakes before each test.
 */
static void mock_power_rule_before(const struct ztest_unit_test *test,
				   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	MOCK_POWER_LIST(RESET_FAKE);

	FFF_RESET_HISTORY();

	power_handle_state_fake.custom_fake = power_handle_state_custom_fake;
	chipset_force_shutdown_fake.custom_fake =
		chipset_force_shutdown_custom_fake;
	chipset_power_on_fake.custom_fake = chipset_power_on_custom_fake;
	command_power_fake.custom_fake = command_power_custom_fake;
}

ZTEST_RULE(mock_power_rule, mock_power_rule_before, NULL);

static const char *power_req_name[POWER_REQ_COUNT] = {
	"none",
	"OFF",
	"ON",
	"SOFT_OFF",
};

static enum power_request_t current_power_request = POWER_REQ_NONE;
static enum power_request_t pending_power_request = POWER_REQ_NONE;

static void handle_power_request(enum power_request_t req)
{
	if (current_power_request == POWER_REQ_NONE) {
		current_power_request = req;
	} else if (current_power_request != req) {
		LOG_INF("MOCK: Handling %s, pend %s request",
			power_req_name[current_power_request],
			power_req_name[req]);
		pending_power_request = req;
	}
}

void mock_power_request(enum power_request_t req)
{
	handle_power_request(req);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));
}

void power_request_complete(void)
{
	current_power_request = pending_power_request;
	pending_power_request = POWER_REQ_NONE;
}

void chipset_force_shutdown_custom_fake(enum chipset_shutdown_reason reason)
{
	LOG_INF("MOCK %s(%d)", __func__, reason);
	handle_power_request(POWER_REQ_OFF);
	task_wake(TASK_ID_CHIPSET);
}

void chipset_power_on_custom_fake(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		handle_power_request(POWER_REQ_ON);
		task_wake(TASK_ID_CHIPSET);
	}
}

/* Power states that we can report */
enum power_state_t {
	PSTATE_UNKNOWN,
	PSTATE_OFF,
	PSTATE_ON,
	PSTATE_COUNT,
};

static const char *const state_name[] = {
	"unknown",
	"OFF",
	"ON",
};

int command_power_custom_fake(int argc, const char **argv)
{
	int v, req;

	if (argc < 2) {
		enum power_state_t state;

		state = PSTATE_UNKNOWN;
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			state = PSTATE_OFF;
		if (chipset_in_state(CHIPSET_STATE_ON))
			state = PSTATE_ON;
		ccprintf("%s\n", state_name[state]);

		return EC_SUCCESS;
	}

	if (!parse_bool(argv[1], &v)) {
		return EC_ERROR_PARAM1;
	}
	req = v ? POWER_REQ_ON : POWER_REQ_OFF;
	handle_power_request(req);
	LOG_INF("MOCK: Requesting power %s\n", power_req_name[req]);
	task_wake(TASK_ID_CHIPSET);

	return EC_SUCCESS;
}

static void mock_lid_event(void)
{
	/* Power task only cares about lid-open events */
	if (!lid_is_open()) {
		return;
	}

	LOG_INF("MOCK: lid opened %s\n", power_req_name[POWER_REQ_ON]);
	handle_power_request(POWER_REQ_ON);
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, mock_lid_event, HOOK_PRIO_DEFAULT);

enum power_state power_handle_state_custom_fake(enum power_state state)
{
	enum power_state new_state = state;
	enum power_request_t request = current_power_request;

	switch (state) {
	/* Steady states */
	case POWER_G3:
		if (current_power_request == POWER_REQ_ON) {
			new_state = POWER_G3S5;
		} else if (current_power_request == POWER_REQ_OFF) {
			new_state = state;
			power_request_complete();
		}
		break;
	case POWER_S5: /* System is soft-off */
		if (current_power_request == POWER_REQ_ON) {
			new_state = POWER_S5S3;
		} else if (current_power_request == POWER_REQ_OFF) {
			/* S5 timeout should transition to G3 */
		} else if (current_power_request == POWER_REQ_SOFT_OFF) {
			power_request_complete();
		}
		break;
	case POWER_S3: /* Suspend; RAM on, processor is asleep */
		if (current_power_request == POWER_REQ_ON) {
			new_state = POWER_S3S0;
		} else if (current_power_request == POWER_REQ_OFF ||
			   current_power_request == POWER_REQ_SOFT_OFF) {
			new_state = POWER_S3S5;
		}
		break;
	case POWER_S0: /* System is on */
		if (current_power_request == POWER_REQ_ON) {
			new_state = state;
			power_request_complete();

			sleep_notify_transition(SLEEP_NOTIFY_RESUME,
						HOOK_CHIPSET_RESUME);
		} else if (current_power_request == POWER_REQ_OFF ||
			   current_power_request == POWER_REQ_SOFT_OFF) {
			new_state = POWER_S0S3;
		}
		break;
	case POWER_S4: /* System is suspended to disk */
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
#endif
		new_state = state;
		break;
	/* Transitions */
	case POWER_S3S0: /* S3 -> S0 */
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		hook_notify(HOOK_CHIPSET_RESUME_INIT);
#endif
		hook_notify(HOOK_CHIPSET_RESUME);
		sleep_resume_transition();
		power_request_complete();
		disable_sleep(SLEEP_MASK_AP_RUN);
		new_state = POWER_S0;
		break;
	case POWER_S0S3: /* S0 -> S3 */
		sleep_notify_transition(SLEEP_NOTIFY_SUSPEND,
					HOOK_CHIPSET_SUSPEND);
		hook_notify(HOOK_CHIPSET_SUSPEND);
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		hook_notify(HOOK_CHIPSET_SUSPEND_COMPLETE);
#endif
		sleep_suspend_transition();
		enable_sleep(SLEEP_MASK_AP_RUN);
		new_state = POWER_S3;
		break;
	case POWER_S5S3: /* S5 -> S3 (skips S4 on non-Intel systems) */
		hook_notify(HOOK_CHIPSET_PRE_INIT);
		hook_notify(HOOK_CHIPSET_STARTUP);
		sleep_reset_tracking();
		new_state = POWER_S3;
		break;
	case POWER_S3S5: /* S3 -> S5 (skips S4 on non-Intel systems) */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
		new_state = POWER_S5;
		break;
	case POWER_G3S5: /* G3 -> S5 (at system init time) */
		new_state = POWER_S5;
		break;
	case POWER_S5G3: /* S5 -> G3 */
		new_state = POWER_G3;
		break;
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ixS0: /* S0ix -> S0 */
		new_state = POWER_S0;
		break;
	case POWER_S0S0ix: /* S0 -> S0ix */
		new_state = POWER_S0ix;
		break;
#endif
	case POWER_S5S4: /* S5 -> S4 */
	case POWER_S4S3: /* S4 -> S3 */
	case POWER_S3S4: /* S3 -> S4 */
	case POWER_S4S5: /* S4 -> S5 */
	default:
		break;
	}

	LOG_INF("MOCK: power request=%u, state=%u -> new_state=%u\n", request,
		state, new_state);

	return new_state;
}
