/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/nx20p348x_public.h"
#include "emul/emul_nx20p348x.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd_tcpm.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define NX20P383X_NODE DT_NODELABEL(nx20p348x_emul)

#define TEST_PORT USBC_PORT_C0

struct nx20p348x_driver_fixture {
	const struct emul *nx20p348x_emul;
};

static void *nx20p348x_driver_setup(void)
{
	static struct nx20p348x_driver_fixture fix;

	fix.nx20p348x_emul = EMUL_DT_GET(NX20P383X_NODE);

	return &fix;
}

ZTEST_SUITE(nx20p348x_driver, drivers_predicate_post_main,
	    nx20p348x_driver_setup, NULL, NULL, NULL);

struct curr_limit_pair {
	enum tcpc_rp_value rp;
	uint8_t reg;
};

/* Note: Register values are slightly higher to account for overshoot */
static struct curr_limit_pair currents[] = {
	{ .rp = TYPEC_RP_3A0, .reg = NX20P348X_ILIM_3_200 },
	{ .rp = TYPEC_RP_1A5, .reg = NX20P348X_ILIM_1_600 },
	{ .rp = TYPEC_RP_USB, .reg = NX20P348X_ILIM_0_600 },
};

ZTEST_F(nx20p348x_driver, test_source_curr_limits)
{
	for (int i = 0; i < ARRAY_SIZE(currents); i++) {
		uint8_t read;

		ppc_set_vbus_source_current_limit(TEST_PORT, currents[i].rp);
		read = nx20p348x_emul_peek(fixture->nx20p348x_emul,
					   NX20P348X_5V_SRC_OCP_THRESHOLD_REG);
		zassert_equal(
			(read & NX20P348X_ILIM_MASK), currents[i].reg,
			"Failed to see correct threshold for Rp %d (reg: 0x%02x)",
			currents[i].rp, read);
	}
}

ZTEST_F(nx20p348x_driver, test_discharge_vbus)
{
	uint8_t reg;

	zassert_ok(ppc_discharge_vbus(TEST_PORT, true));
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_DEVICE_CONTROL_REG);
	zassert_equal((reg & NX20P348X_CTRL_VBUSDIS_EN),
		      NX20P348X_CTRL_VBUSDIS_EN);

	zassert_ok(ppc_discharge_vbus(TEST_PORT, false));
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_DEVICE_CONTROL_REG);
	zassert_not_equal((reg & NX20P348X_CTRL_VBUSDIS_EN),
			  NX20P348X_CTRL_VBUSDIS_EN);
}

ZTEST(nx20p348x_driver, test_sink_enable_timeout_failure)
{
	/* Note: PPC requires a TCPC GPIO to enable its sinking */
	zassert_equal(ppc_vbus_sink_enable(TEST_PORT, true), EC_ERROR_TIMEOUT);
}

ZTEST(nx20p348x_driver, test_source_enable_timeout_failure)
{
	/* Note: PPC requires a TCPC GPIO to enable its sourcing */
	zassert_equal(ppc_vbus_source_enable(TEST_PORT, true),
		      EC_ERROR_TIMEOUT);
}

ZTEST(nx20p348x_driver, test_ppc_dump)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	/* This chip supports PPC dump, so should return success */
	zassert_ok(shell_execute_cmd(shell_zephyr, "ppc_dump 0"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0);

	/* Weakly verify something reasonable was output to console */
	zassert_not_null(strstr(outbuffer, "]: 0x"));
}

ZTEST_F(nx20p348x_driver, test_db_exit_err)
{
	uint8_t reg;

	/* Test an error to exit dead battery mode */
	nx20p348x_emul_set_interrupt1(fixture->nx20p348x_emul,
				      NX20P348X_INT1_DBEXIT_ERR);

	/* Give the interrupt time to process */
	k_sleep(K_MSEC(500));

	/* Interrupt should have set DB exit in the control register */
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_DEVICE_CONTROL_REG);
	zassert_equal((reg & NX20P348X_CTRL_DB_EXIT), NX20P348X_CTRL_DB_EXIT);
}

ZTEST_F(nx20p348x_driver, test_db_exit_err_max)
{
	uint8_t reg;

	/* Set a DB exit error 10 times */
	for (int i = 0; i < 10; i++) {
		nx20p348x_emul_set_interrupt1(fixture->nx20p348x_emul,
					      NX20P348X_INT1_DBEXIT_ERR);
		k_sleep(K_MSEC(500));
	}

	/* Interrupt should now be masked by the driver */
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_INTERRUPT1_MASK_REG);
	zassert_equal((reg & NX20P348X_INT1_DBEXIT_ERR),
		      NX20P348X_INT1_DBEXIT_ERR);
}

/* Add filler in case of event data */
#define MAX_RESPONSE_PD_LOG_ENTRY_SIZE (sizeof(struct ec_response_pd_log) + 16)

static void flush_pd_log(void)
{
	uint8_t response_buffer[MAX_RESPONSE_PD_LOG_ENTRY_SIZE];
	struct ec_response_pd_log *response =
		(struct ec_response_pd_log *)response_buffer;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PD_GET_LOG_ENTRY, 0);

	args.response = response;
	args.response_max = sizeof(response_buffer);

	for (int i = 0; i < 10; i++) {
		zassert_ok(host_command_process(&args));

		if (response->type == PD_EVENT_NO_ENTRY)
			return;

		k_sleep(K_MSEC(500));
	}

	zassert_unreachable("Failed to flush PD log");
}

ZTEST_F(nx20p348x_driver, test_vbus_overcurrent)
{
	uint8_t response_buffer[MAX_RESPONSE_PD_LOG_ENTRY_SIZE];
	struct ec_response_pd_log *response =
		(struct ec_response_pd_log *)response_buffer;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PD_GET_LOG_ENTRY, 0);

	flush_pd_log();

	/* Set up overcurrent */
	nx20p348x_emul_set_interrupt1(fixture->nx20p348x_emul,
				      NX20P348X_INT1_OC_5VSRC);
	k_sleep(K_MSEC(500));

	args.response = response;
	args.response_max = sizeof(response_buffer);

	zassert_ok(host_command_process(&args));
	zassert_equal(TEST_PORT, PD_LOG_PORT(response->size_port));
	zassert_equal(0, PD_LOG_SIZE(response->size_port));
	zassert_equal(PD_EVENT_PS_FAULT, response->type);
	zassert_equal(PS_FAULT_OCP, response->data);
}

ZTEST_F(nx20p348x_driver, test_vbus_reverse_current)
{
	uint8_t response_buffer[MAX_RESPONSE_PD_LOG_ENTRY_SIZE];
	struct ec_response_pd_log *response =
		(struct ec_response_pd_log *)response_buffer;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PD_GET_LOG_ENTRY, 0);

	flush_pd_log();

	/* Set up reverse current */
	nx20p348x_emul_set_interrupt1(fixture->nx20p348x_emul,
				      NX20P348X_INT1_RCP_5VSRC);
	k_sleep(K_MSEC(500));

	args.response = response;
	args.response_max = sizeof(response_buffer);

	zassert_ok(host_command_process(&args));
	zassert_equal(TEST_PORT, PD_LOG_PORT(response->size_port));
	zassert_equal(0, PD_LOG_SIZE(response->size_port));
	zassert_equal(PD_EVENT_PS_FAULT, response->type);
	zassert_equal(PS_FAULT_OCP, response->data);
}

ZTEST_F(nx20p348x_driver, test_vbus_short)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	/* Set up Vbus short, which we only report in the console */
	nx20p348x_emul_set_interrupt1(fixture->nx20p348x_emul,
				      NX20P348X_INT1_SC_5VSRC);
	k_sleep(K_MSEC(500));

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0);

	/* Weakly verify something reasonable was output to console */
	zassert_not_null(strstr(outbuffer, "short"));
}
