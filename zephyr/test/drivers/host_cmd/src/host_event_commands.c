/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>
#include "include/lpc.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

struct host_cmd_host_event_commands_fixture {
	struct host_events_ctx ctx;
};

static void *host_cmd_host_event_commands_setup(void)
{
	static struct host_cmd_host_event_commands_fixture fixture = { 0 };

	return &fixture;
}

static void host_cmd_host_event_commands_before(void *fixture)
{
	struct host_cmd_host_event_commands_fixture *f = fixture;

	host_events_save(&f->ctx);
}

static void host_cmd_host_event_commands_after(void *fixture)
{
	struct host_cmd_host_event_commands_fixture *f = fixture;

	host_events_restore(&f->ctx);
}

ZTEST_SUITE(host_cmd_host_event_commands, drivers_predicate_post_main,
	    host_cmd_host_event_commands_setup,
	    host_cmd_host_event_commands_before,
	    host_cmd_host_event_commands_after, NULL);

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT invalid host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_invalid_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };

	ret_val = host_cmd_host_event(0xFF, 0, &result);

	zassert_equal(ret_val, EC_RES_INVALID_PARAM, "Expected=%d, returned=%d",
		      EC_RES_INVALID_PARAM, ret_val);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT get host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_get_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };
	struct {
		uint8_t mask;
		enum ec_status result;
	} event_get[] = {
		{ EC_HOST_EVENT_MAIN, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_B, EC_RES_SUCCESS },
#ifdef CONFIG_HOSTCMD_X86
		{ EC_HOST_EVENT_SCI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_SMI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ALWAYS_REPORT_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ACTIVE_WAKE_MASK, EC_RES_SUCCESS },
#ifdef CONFIG_POWER_S0IX
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX, EC_RES_SUCCESS },
#endif /* CONFIG_POWER_S0IX */
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S3, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S5, EC_RES_SUCCESS },
#endif /* CONFIG_HOSTCMD_X86 */
		{ 0xFF, EC_RES_INVALID_PARAM },
	};

	for (int i = 0; i < ARRAY_SIZE(event_get); i++) {
		ret_val = host_cmd_host_event(EC_HOST_EVENT_GET,
					      event_get[i].mask, &result);
		zassert_equal(ret_val, event_get[i].result,
			      "[%d] Expected=%d, returned=%d", i,
			      event_get[i].result, ret_val);
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT set host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_set_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };
	struct {
		uint8_t mask;
		enum ec_status result;
	} event_set[] = {
		{ EC_HOST_EVENT_MAIN, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_B, EC_RES_ACCESS_DENIED },
#ifdef CONFIG_HOSTCMD_X86
		{ EC_HOST_EVENT_SCI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_SMI_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ALWAYS_REPORT_MASK, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_ACTIVE_WAKE_MASK, EC_RES_SUCCESS },
#ifdef CONFIG_POWER_S0IX
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX, EC_RES_SUCCESS },
#endif /* CONFIG_POWER_S0IX */
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S3, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S5, EC_RES_SUCCESS },
#endif /* CONFIG_HOSTCMD_X86 */
		{ 0xFF, EC_RES_INVALID_PARAM },
	};

	for (int i = 0; i < ARRAY_SIZE(event_set); i++) {
		ret_val = host_cmd_host_event(EC_HOST_EVENT_SET,
					      event_set[i].mask, &result);
		zassert_equal(ret_val, event_set[i].result,
			      "[%d] Expected=%d, returned=%d", i,
			      event_set[i].result, ret_val);
	}
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT clear host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_clear_cmd)
{
	enum ec_status ret_val;
	struct ec_response_host_event result = { 0 };
	struct {
		uint8_t mask;
		enum ec_status result;
	} event_set[] = {
		{ EC_HOST_EVENT_MAIN, EC_RES_SUCCESS },
		{ EC_HOST_EVENT_B, EC_RES_SUCCESS },
#ifdef CONFIG_HOSTCMD_X86
		{ EC_HOST_EVENT_SCI_MASK, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_SMI_MASK, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_ALWAYS_REPORT_MASK, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_ACTIVE_WAKE_MASK, EC_RES_ACCESS_DENIED },
#ifdef CONFIG_POWER_S0IX
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX, EC_RES_ACCESS_DENIED },
#endif /* CONFIG_POWER_S0IX */
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S3, EC_RES_ACCESS_DENIED },
		{ EC_HOST_EVENT_LAZY_WAKE_MASK_S5, EC_RES_ACCESS_DENIED },
#endif /* CONFIG_HOSTCMD_X86 */
		{ 0xFF, EC_RES_INVALID_PARAM },
	};

	for (int i = 0; i < ARRAY_SIZE(event_set); i++) {
		ret_val = host_cmd_host_event(EC_HOST_EVENT_CLEAR,
					      event_set[i].mask, &result);
		zassert_equal(ret_val, event_set[i].result,
			      "Expected [%d] result=%d, returned=%d", i,
			      event_set[i].result, ret_val);
	}
}

enum ec_status host_event_mask_cmd_helper(uint32_t command, uint32_t mask,
					  struct ec_response_host_event_mask *r)
{
	enum ec_status ret_val;

	struct ec_params_host_event_mask params = {
		.mask = mask,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(command, 0, *r, params);

	ret_val = host_command_process(&args);

	return ret_val;
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_CLEAR clear host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_clear__cmd)
{
	enum ec_status ret_val;
	host_event_t events;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY);
	host_event_t lpc_event_mask;
	struct ec_response_host_event_mask response = { 0 };

	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, lpc_event_mask | mask);

	host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);
	events = host_get_events();

	zassert_true(events & mask, "events=0x%X", events);

	ret_val = host_event_mask_cmd_helper(EC_CMD_HOST_EVENT_CLEAR, mask,
					     &response);

	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);

	events = host_get_events();
	zassert_false(events & mask, "events=0x%X", events);
}

/**
 * @brief TestPurpose: Verify EC_CMD_HOST_EVENT_CLEAR_B clear host command.
 */
ZTEST_USER(host_cmd_host_event_commands, test_host_event_clear_b_cmd)
{
	enum ec_status ret_val;
	host_event_t events_b;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY);
	host_event_t lpc_event_mask;
	struct ec_response_host_event_mask response = { 0 };
	struct ec_response_host_event result = { 0 };

	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, lpc_event_mask | mask);

	host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	host_cmd_host_event(EC_HOST_EVENT_GET, EC_HOST_EVENT_B, &result);
	events_b = result.value;
	zassert_true(events_b & mask, "events_b=0x%X", events_b);

	ret_val = host_event_mask_cmd_helper(EC_CMD_HOST_EVENT_CLEAR_B, mask,
					     &response);

	zassert_equal(ret_val, EC_RES_SUCCESS, "Expected %d, returned %d",
		      EC_RES_SUCCESS, ret_val);

	host_cmd_host_event(EC_HOST_EVENT_GET, EC_HOST_EVENT_B, &result);
	events_b = result.value;
	zassert_false(events_b & mask, "events_b=0x%X", events_b);
}
