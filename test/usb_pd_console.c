/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test usb_pd_console
 */

#include "common.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "test_util.h"
#include "usb_pd.h"
#include "usb_pe_sm.h"
#include "usb_tc_sm.h"
#include "util.h"

/* Defined in implementation */
int command_pd(int argc, const char **argv);

static enum debug_level dpm_debug_level;
static enum debug_level prl_debug_level;
static enum debug_level pe_debug_level;
static enum debug_level tc_debug_level;

static bool pd_get_polarity_called;
static bool pd_comm_is_enabled_called;
static bool pd_get_power_role_called;
static bool pd_get_data_role_called;
static bool tc_is_vconn_src_called;
static bool tc_get_current_state_called;
static bool tc_get_flags_called;
static bool pe_get_current_state_called;
static bool pe_get_flags_called;
static bool pe_is_explicit_contract_called;
static bool pe_snk_in_epr_mode_called;
static bool pe_snk_epr_explicit_exit_called;
static bool pd_get_dual_role_called;
static bool board_get_usb_pd_port_count_called;
static bool pd_srccaps_dump_called;
static bool pd_timer_dump_called;

static enum try_src_override_t try_src_override;
static int test_port;
static enum pd_dpm_request request;
static int max_volt;
static int comm_enable;
static int pd_suspended;
static int pd_bistsharemode;
static int dev_info;
static int vdm_cmd;
static int vdm_count;
static int vdm_vid;
static uint32_t vdm_data[10];
static enum pd_dual_role_states dr_state;
static enum pd_power_role pd_get_power_role_return = PD_ROLE_SINK;

/* Mock functions */
int tc_is_vconn_src(int port)
{
	tc_is_vconn_src_called = true;
	return 0;
}

void pd_request_vconn_swap(int port)
{
	test_port = port;
	pd_dpm_request(port, DPM_REQUEST_VCONN_SWAP);
}

enum pd_power_role pd_get_power_role(int port)
{
	pd_get_power_role_called = true;
	return pd_get_power_role_return;
}

uint32_t pe_get_flags(int port)
{
	pe_get_flags_called = true;
	return 0;
}

int pe_is_explicit_contract(int port)
{
	pe_is_explicit_contract_called = true;
	return 1;
}

bool pe_snk_in_epr_mode(int port)
{
	pe_snk_in_epr_mode_called = true;
	return true;
}

void pe_snk_epr_explicit_exit(int port)
{
	pe_snk_epr_explicit_exit_called = true;
}

const char *pe_get_current_state(int port)
{
	pe_get_current_state_called = true;
	return "PE_STATE";
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	pd_get_dual_role_called = true;
	return 0;
}

void pd_timer_dump(int port)
{
	pd_timer_dump_called = true;
}

void pd_srccaps_dump(int port)
{
	pd_srccaps_dump_called = true;
}

void dpm_set_debug_level(enum debug_level level)
{
	dpm_debug_level = level;
}

void prl_set_debug_level(enum debug_level level)
{
	prl_debug_level = level;
}

void pe_set_debug_level(enum debug_level level)
{
	pe_debug_level = level;
}

void tc_set_debug_level(enum debug_level level)
{
	tc_debug_level = level;
}

enum pd_data_role pd_get_data_role(int port)
{
	pd_get_data_role_called = true;
	return 0;
}

void tc_state_init(int port)
{
}

void tc_event_check(int port, int evt)
{
}

uint8_t tc_get_pd_enabled(int port)
{
	return 0;
}

void pe_run(int port, int evt, int en)
{
}

void tc_run(int port)
{
}

uint8_t board_get_usb_pd_port_count(void)
{
	board_get_usb_pd_port_count_called = true;
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

void pe_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
		 int count)
{
	int i;

	test_port = port;
	vdm_cmd = cmd;
	vdm_count = count;
	vdm_vid = vid;

	if (data == NULL)
		for (i = 0; i < 10; i++)
			vdm_data[i] = -1;
	else
		for (i = 0; i < count; i++)
			vdm_data[i] = data[i];
}

void pd_dpm_request(int port, enum pd_dpm_request req)
{
	test_port = port;
	request = req;
}

unsigned int pd_get_max_voltage(void)
{
	return 10000;
}

void pd_request_source_voltage(int port, int mv)
{
	test_port = port;
	max_volt = mv;
}

void pd_comm_enable(int port, int enable)
{
	test_port = port;
	comm_enable = enable;
}

void pd_set_suspend(int port, int enable)
{
	test_port = port;
	pd_suspended = enable;
}

enum ec_status pd_set_bist_share_mode(uint8_t enable)
{
	pd_bistsharemode = enable;
	return EC_RES_SUCCESS;
}

void tc_print_dev_info(int port)
{
	test_port = port;
	dev_info = 1;
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	test_port = port;
	dr_state = state;
}

int pd_comm_is_enabled(int port)
{
	test_port = port;
	pd_comm_is_enabled_called = true;
	return 0;
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	test_port = port;
	pd_get_polarity_called = true;
	return 0;
}

uint32_t tc_get_flags(int port)
{
	test_port = port;
	tc_get_flags_called = true;
	return 0;
}

const char *tc_get_current_state(int port)
{
	test_port = port;
	tc_get_current_state_called = true;
	return "TC_STATE";
}

void tc_try_src_override(enum try_src_override_t ov)
{
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC)) {
		switch (ov) {
		case TRY_SRC_OVERRIDE_OFF: /* 0 */
			try_src_override = TRY_SRC_OVERRIDE_OFF;
			break;
		case TRY_SRC_OVERRIDE_ON: /* 1 */
			try_src_override = TRY_SRC_OVERRIDE_ON;
			break;
		default:
			try_src_override = TRY_SRC_NO_OVERRIDE;
		}
	}
}

enum try_src_override_t tc_get_try_src_override(void)
{
	return try_src_override;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return PD_CC_NONE;
}

static int test_command_pd_dump(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "dump", "", 0, 0, 0 };
	char test[2];

	sprintf(test, "e");
	argv[2] = test;
	TEST_ASSERT(command_pd(argc, argv) == EC_ERROR_PARAM2);

	for (int i = DEBUG_DISABLE; i <= DEBUG_LEVEL_MAX; i++) {
		sprintf(test, "%d", i);
		argv[2] = test;
		TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
		TEST_ASSERT(dpm_debug_level == i);
		TEST_ASSERT(prl_debug_level == i);
		TEST_ASSERT(pe_debug_level == i);
		TEST_ASSERT(tc_debug_level == i);
	}

	sprintf(test, "%d", DEBUG_LEVEL_MAX + 1);
	argv[2] = test;
	TEST_ASSERT(dpm_debug_level == DEBUG_LEVEL_MAX);
	TEST_ASSERT(prl_debug_level == DEBUG_LEVEL_MAX);
	TEST_ASSERT(pe_debug_level == DEBUG_LEVEL_MAX);
	TEST_ASSERT(tc_debug_level == DEBUG_LEVEL_MAX);

	return EC_SUCCESS;
}

static int test_command_pd_try_src(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "trysrc", "2", 0, 0 };

	try_src_override = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(try_src_override == TRY_SRC_NO_OVERRIDE);

	argv[2] = "1";
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(try_src_override == TRY_SRC_OVERRIDE_ON);

	argv[2] = "0";
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(try_src_override == TRY_SRC_OVERRIDE_OFF);

	return EC_SUCCESS;
}

static int test_command_pd_version(void)
{
	int argc = 2;
	const char *argv[] = { "pd", "version", 0, 0, 0 };

	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_command_pd_bistsharemode_enable(void)
{
	int argc = 3;
	static const char *argv[10] = { "pd", "bistsharemode", "enable" };

	pd_bistsharemode = -1;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_bistsharemode == 1);

	return EC_SUCCESS;
}

static int test_command_pd_bistsharemode_disable(void)
{
	int argc = 3;
	static const char *argv[10] = { "pd", "bistsharemode", "disable" };

	pd_bistsharemode = -1;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_bistsharemode == 0);

	return EC_SUCCESS;
}

static int test_command_pd_arg_count(void)
{
	int argc;
	const char *argv[] = { "pd", "", 0, 0, 0 };

	for (argc = 0; argc < 3; argc++)
		TEST_ASSERT(command_pd(argc, argv) == EC_ERROR_PARAM_COUNT);

	return EC_SUCCESS;
}

static int test_command_pd_port_num(void)
{
	int argc = 3;
	const char *argv[10] = { "pd", "0", 0, 0, 0 };
	char test[2];

	sprintf(test, "%d", CONFIG_USB_PD_PORT_MAX_COUNT);
	argv[1] = test;
	TEST_ASSERT(command_pd(argc, argv) == EC_ERROR_PARAM2);

	return EC_SUCCESS;
}

static int test_command_pd_tx(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "tx", 0, 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(request == DPM_REQUEST_SNK_STARTUP);

	return EC_SUCCESS;
}

static int test_command_pd_charger(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "1", "charger", 0, 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 1);
	TEST_ASSERT(request == DPM_REQUEST_SRC_STARTUP);

	return EC_SUCCESS;
}

static int test_command_pd_dev1(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "dev", "20", 0 };

	request = 0;
	max_volt = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(request == DPM_REQUEST_NEW_POWER_LEVEL);
	TEST_ASSERT(max_volt == 20000);

	return EC_SUCCESS;
}

static int test_command_pd_dev2(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "1", "dev", 0, 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 1);
	TEST_ASSERT(request == DPM_REQUEST_NEW_POWER_LEVEL);
	TEST_ASSERT(max_volt == 10000);

	return EC_SUCCESS;
}

static int test_command_pd_disable(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "disable", 0, 0 };

	comm_enable = 1;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(comm_enable == 0);

	return EC_SUCCESS;
}

static int test_command_pd_enable(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "1", "enable", 0, 0 };

	comm_enable = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 1);
	TEST_ASSERT(comm_enable == 1);

	return EC_SUCCESS;
}

static int test_command_pd_hard(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "hard", 0, 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(request == DPM_REQUEST_HARD_RESET_SEND);

	return EC_SUCCESS;
}

static int test_command_pd_soft(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "soft", 0, 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(request == DPM_REQUEST_SOFT_RESET_SEND);

	return EC_SUCCESS;
}

static int test_command_pd_suspend(void)
{
	int argc = 3;
	static const char *argv[] = { "pd", "0", "suspend" };

	test_port = -1;
	comm_enable = -1;
	pd_suspended = -1;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(comm_enable == 0);
	TEST_ASSERT(pd_suspended == 1);

	return EC_SUCCESS;
}

static int test_command_pd_resume(void)
{
	int argc = 3;
	static const char *argv[] = { "pd", "1", "resume" };

	test_port = -1;
	comm_enable = -1;
	pd_suspended = -1;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 1);
	TEST_ASSERT(comm_enable == 1);
	TEST_ASSERT(pd_suspended == 0);

	return EC_SUCCESS;
}

static int test_command_pd_swap1(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "swap", 0, 0 };

	TEST_ASSERT(command_pd(argc, argv) == EC_ERROR_PARAM_COUNT);

	return EC_SUCCESS;
}

static int test_command_pd_swap2(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "swap", "power", 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(request == DPM_REQUEST_PR_SWAP);

	return EC_SUCCESS;
}

static int test_command_pd_swap3(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "1", "swap", "data", 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 1);
	TEST_ASSERT(request == DPM_REQUEST_DR_SWAP);

	return EC_SUCCESS;
}

static int test_command_pd_swap4(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "swap", "vconn", 0 };

	request = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(request == DPM_REQUEST_VCONN_SWAP);

	return EC_SUCCESS;
}

static int test_command_pd_swap5(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "swap", "xyz", 0 };

	TEST_ASSERT(command_pd(argc, argv) == EC_ERROR_PARAM3);

	return EC_SUCCESS;
}

static int test_command_pd_dualrole0(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "dualrole", 0, 0 };

	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_command_pd_dualrole1(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "dualrole", "on", 0 };

	dr_state = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(dr_state == PD_DRP_TOGGLE_ON);

	return EC_SUCCESS;
}

static int test_command_pd_dualrole2(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "dualrole", "off", 0 };

	dr_state = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(dr_state == PD_DRP_TOGGLE_OFF);

	return EC_SUCCESS;
}

static int test_command_pd_dualrole3(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "dualrole", "freeze", 0 };

	dr_state = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(dr_state == PD_DRP_FREEZE);

	return EC_SUCCESS;
}

static int test_command_pd_dualrole4(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "dualrole", "sink", 0 };

	dr_state = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(dr_state == PD_DRP_FORCE_SINK);

	return EC_SUCCESS;
}

static int test_command_pd_dualrole5(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "dualrole", "source", 0 };

	dr_state = 0;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(test_port == 0);
	TEST_ASSERT(dr_state == PD_DRP_FORCE_SOURCE);

	return EC_SUCCESS;
}

static int test_command_pd_state(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "state", 0, 0 };

	pd_get_polarity_called = false;
	pd_comm_is_enabled_called = false;
	pd_get_power_role_called = false;
	pd_get_data_role_called = false;
	tc_is_vconn_src_called = false;
	tc_get_current_state_called = false;
	tc_get_flags_called = false;
	pe_get_current_state_called = false;
	pe_get_flags_called = false;
	pe_is_explicit_contract_called = true;
	pe_snk_in_epr_mode_called = true;

	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_get_polarity_called);
	TEST_ASSERT(pd_comm_is_enabled_called);
	TEST_ASSERT(pd_get_power_role_called);
	TEST_ASSERT(pd_get_data_role_called);
	TEST_ASSERT(tc_is_vconn_src_called);
	TEST_ASSERT(tc_get_current_state_called);
	TEST_ASSERT(tc_get_flags_called);
	TEST_ASSERT(pe_get_current_state_called);
	TEST_ASSERT(pe_get_flags_called);
	TEST_ASSERT(pe_is_explicit_contract_called);
	TEST_ASSERT(pe_snk_in_epr_mode_called);

	return EC_SUCCESS;
}

static int test_command_pd_srccaps(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "srccaps", 0, 0 };

	pd_srccaps_dump_called = false;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_srccaps_dump_called);

	return EC_SUCCESS;
}

static int test_command_pd_epr(void)
{
	int argc = 4;
	const char *argv[] = { "pd", "0", "epr", "enter" };

	/* Return EC_ERROR_PARAM_COUNT if argc < 3. */
	TEST_ASSERT(command_pd(3, argv) == EC_ERROR_PARAM_COUNT);

	/* Return SUCCESS with request==0 if role==PD_ROLE_SOURCE. */
	request = 0;
	pd_get_power_role_called = false;
	pd_get_power_role_return = PD_ROLE_SOURCE;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_get_power_role_called);
	TEST_ASSERT(request == 0);

	/* Return SUCCESS with request==DPM_REQUEST_EPR_MODE_ENTRY. */
	request = 0;
	pd_get_power_role_called = false;
	pd_get_power_role_return = PD_ROLE_SINK;
	pe_snk_epr_explicit_exit_called = false;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_get_power_role_called);
	TEST_ASSERT(request == DPM_REQUEST_EPR_MODE_ENTRY);
	TEST_ASSERT(!pe_snk_epr_explicit_exit_called);

	/* Return SUCCESS with request==DPM_REQUEST_EPR_MODE_EXIT. */
	argv[3] = "exit";
	request = 0;
	pd_get_power_role_called = false;
	pd_get_power_role_return = PD_ROLE_SINK;
	pe_snk_epr_explicit_exit_called = false;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_get_power_role_called);
	TEST_ASSERT(request == DPM_REQUEST_EPR_MODE_EXIT);
	TEST_ASSERT(pe_snk_epr_explicit_exit_called);

	/* Return EC_ERROR_PARAM3 for invalid sub-command. */
	argv[3] = "start";
	TEST_ASSERT(command_pd(argc, argv) == EC_ERROR_PARAM3);

	return EC_SUCCESS;
}

static int test_command_pd_timer(void)
{
	int argc = 3;
	const char *argv[] = { "pd", "0", "timer", 0, 0 };

	pd_timer_dump_called = false;
	TEST_ASSERT(command_pd(argc, argv) == EC_SUCCESS);
	TEST_ASSERT(pd_timer_dump_called);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_command_pd_dump);
	RUN_TEST(test_command_pd_try_src);
	RUN_TEST(test_command_pd_version);
	RUN_TEST(test_command_pd_arg_count);
	RUN_TEST(test_command_pd_port_num);
	RUN_TEST(test_command_pd_tx);
	RUN_TEST(test_command_pd_charger);
	RUN_TEST(test_command_pd_dev1);
	RUN_TEST(test_command_pd_dev2);
	RUN_TEST(test_command_pd_disable);
	RUN_TEST(test_command_pd_enable);
	RUN_TEST(test_command_pd_hard);
	RUN_TEST(test_command_pd_soft);
	RUN_TEST(test_command_pd_suspend);
	RUN_TEST(test_command_pd_resume);
	RUN_TEST(test_command_pd_bistsharemode_enable);
	RUN_TEST(test_command_pd_bistsharemode_disable);
	RUN_TEST(test_command_pd_swap1);
	RUN_TEST(test_command_pd_swap2);
	RUN_TEST(test_command_pd_swap3);
	RUN_TEST(test_command_pd_swap4);
	RUN_TEST(test_command_pd_swap5);
	RUN_TEST(test_command_pd_dualrole0);
	RUN_TEST(test_command_pd_dualrole1);
	RUN_TEST(test_command_pd_dualrole2);
	RUN_TEST(test_command_pd_dualrole3);
	RUN_TEST(test_command_pd_dualrole4);
	RUN_TEST(test_command_pd_dualrole5);
	RUN_TEST(test_command_pd_epr);
	RUN_TEST(test_command_pd_state);
	RUN_TEST(test_command_pd_srccaps);
	RUN_TEST(test_command_pd_timer);

	test_print_result();
}
