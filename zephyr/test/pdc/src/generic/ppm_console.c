/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_ppm_driver.h"
#include "usbc/ppm.h"
#include "usbc/utils.h"

#include <stdbool.h>
#include <stdio.h>

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(ucsi_ppm_console_test, LOG_LEVEL_DBG);

static void reset(void *fixture)
{
	emul_ppm_driver_reset();

	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST_SUITE(ucsi_ppm_console, NULL, NULL, reset, reset, NULL);

ZTEST(ucsi_ppm_console, test_ppm_no_subcmd)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "ppm");
	zassert_equal(rv, SHELL_CMD_HELP_PRINTED, "Expected %d, but got %d",
		      SHELL_CMD_HELP_PRINTED, rv);
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__bad_port)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_alt_modes");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_alt_modes 2");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_alt_modes -1");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__bad_recipient)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_alt_modes 0 blah");
	zassert_equal(rv, 1, "Expected %d, but got %d", 1, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__ucsi_fail)
{
	int rv;

	/* Report an error running the UCSI command */
	ppm_driver_mock_execute_cmd_sync_fake.return_val = -1;

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_alt_modes 0 conn");
	zassert_equal(rv, 1, "Expected %d, but got %d", 1, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

/* Fake alternate modes to report support for */
static const struct {
	uint16_t svid;
	uint32_t mid;
} altmodes[] = {
	{ .svid = 0x1A2B, .mid = 0x12345678 },
	{ .svid = 0x3C4D, .mid = 0x90ABCDEF },
	{ .svid = 0x5E6F, .mid = 0xA1B2C3D4 },
};

/* Expected port and recipient. Checked inside `execute_ucsi_alt_mode()` */
static uint8_t altmode_expected_port = 0;
static uint8_t altmode_expected_recipient = 0;

/**
 * @brief Custom fake to return a UCSI GET_ALTERNATE_MODES response
 */
static int execute_ucsi_alt_mode(const struct device *dev,
				 struct ucsi_control_t *cmd, uint8_t *out)
{
	zassert_equal(0, cmd->data_length,
		      "GET_ALTERNATE_MODES length must be 0");
	zassert_equal(UCSI_GET_ALTERNATE_MODES, cmd->command,
		      "Command must be UCSI_GET_ALTERNATE_MODES");

	zassert_equal(altmode_expected_recipient,
		      cmd->command_specific[0] & 0x7,
		      "Incorrect recipient sent");

	zassert_equal(altmode_expected_port + 1,
		      cmd->command_specific[1] & 0x7F, "Incorrect port sent");

	uint8_t offset = cmd->command_specific[2];
	uint8_t length = cmd->command_specific[3];

	/* Note: length==0 returns 1 object, length==1 returns 2 objects */
	zassert_true(length == 0 || length == 1, "Length must be 0 or 1");

	struct ucsi_get_alternate_modes_t *resp =
		(struct ucsi_get_alternate_modes_t *)out;

	memset(resp, 0, sizeof(*resp));

	if (offset >= ARRAY_SIZE(altmodes)) {
		/* Out of bounds. Return zeroes */
		return sizeof(*resp);
	}

	resp->altmode_fields[0].svid = altmodes[offset].svid;
	resp->altmode_fields[0].mid = altmodes[offset].mid;

	if (length > 0 && (offset + length) < ARRAY_SIZE(altmodes)) {
		/* Requested two objects AND we have another to return */
		resp->altmode_fields[1].svid = altmodes[offset + length].svid;
		resp->altmode_fields[1].mid = altmodes[offset + length].mid;
	}

	/* Always return the full response length, even if some fields are
	 * zero.
	 */
	return sizeof(*resp);
}

/**
 * @brief Call the `ppm get_alt_modes` console command and check output
 */
static void get_alt_mode_helper(const char *cmd, uint8_t expected_port,
				uint8_t expected_recipient)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	/* Set expected values for custom fake to check in UCSI request message
	 */
	altmode_expected_port = expected_port;
	altmode_expected_recipient = expected_recipient;
	ppm_driver_mock_execute_cmd_sync_fake.custom_fake =
		execute_ucsi_alt_mode;

	rv = shell_execute_cmd(get_ec_shell(), cmd);
	zassert_ok(rv, "Expected success (0), but got %d", rv);

	/* Inspect shell output. Expected output:
	 *
	 *    Port: C0 (UCSI port 1), Recipient: X
	 *
	 *    Offset | Alternate mode
	 *    -------+--------------------------------------
	 *    000    | SVID=0x1a2b MID=0x12345678
	 *    001    | SVID=0x3c4d MID=0x90abcdef
	 *    002    | SVID=0x5e6f MID=0xa1b2c3d4
	 */

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	char header_line[80];
	rv = snprintf(header_line, sizeof(header_line),
		      "Port: C%u (UCSI port %u), Recipient: %u", expected_port,
		      expected_port + 1, expected_recipient);
	zassert_true(rv < sizeof(header_line));

	zassert_not_null(strstr(outbuffer, header_line));
	zassert_not_null(
		strstr(outbuffer, "000    | SVID=0x1a2b MID=0x12345678"));
	zassert_not_null(
		strstr(outbuffer, "001    | SVID=0x3c4d MID=0x90abcdef"));
	zassert_not_null(
		strstr(outbuffer, "002    | SVID=0x5e6f MID=0xa1b2c3d4"));
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__none)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	/* Return no alternate modes. The UCSI command execute function
	 * will return 0 bytes by default.
	 */

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_alt_modes 0");
	zassert_ok(rv, "Expected success (0), but got %d", rv);

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "No alternate modes reported"));
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__success)
{
	/* No 3rd arg implies `sop` (== 1) as recipient */

	get_alt_mode_helper("ppm get_alt_modes 0", /* port= */ 0,
			    /* recipient= */ 1);
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__success_conn)
{
	get_alt_mode_helper("ppm get_alt_modes 0 conn", /* port= */ 0,
			    /* recipient= */ 0);
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__success_sop)
{
	get_alt_mode_helper("ppm get_alt_modes 0 sop", /* port= */ 0,
			    /* recipient= */ 1);
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__success_sopprime)
{
	get_alt_mode_helper("ppm get_alt_modes 0 sopprime", /* port= */ 0,
			    /* recipient= */ 2);
}

ZTEST(ucsi_ppm_console, test_ppm_get_alt_modes__success_sopprimeprime)
{
	get_alt_mode_helper("ppm get_alt_modes 0 sopprimeprime", /* port= */ 0,
			    /* recipient= */ 3);
}

ZTEST(ucsi_ppm_console, test_ppm_get_cam_supported__bad_port)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_cam_supported");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_cam_supported 2");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_cam_supported -1");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_get_cam_supported__ucsi_fail)
{
	int rv;

	/* Report an error running the UCSI command */
	ppm_driver_mock_execute_cmd_sync_fake.return_val = -1;

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_cam_supported 0");
	zassert_equal(rv, 1, "Expected %d, but got %d", 1, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

/**
 * @brief Custom fake to return a UCSI GET_CAM_SUPPORTED response
 */
static int execute_ucsi_get_cam_supported(const struct device *dev,
					  struct ucsi_control_t *cmd,
					  uint8_t *out)
{
	zassert_equal(0, cmd->data_length,
		      "GET_CAM_SUPPORTED length must be 0");
	zassert_equal(UCSI_GET_CAM_SUPPORTED, cmd->command,
		      "Command must be UCSI_GET_CAM_SUPPORTED");

	/* UCSI ports are 1-indexed */
	zassert_equal(1, cmd->command_specific[0] & 0x7F,
		      "Incorrect port sent");

	/* Has bits 0, 7, 8, 10, 12, 14 set (arbitrary) */
	out[0] = 0x81;
	out[1] = 0x55;

	/* Two bytes written out */
	return 2;
}

ZTEST(ucsi_ppm_console, test_ppm_get_cam_supported__success)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	ppm_driver_mock_execute_cmd_sync_fake.custom_fake =
		execute_ucsi_get_cam_supported;

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_cam_supported 0");
	zassert_ok(rv, "Expected success (0), but got %d", rv);

	/* Inspect shell output. Expected output:
	 *
	 *    Port: C0 (UCSI port 1), Supported:
	 *
	 *    Supported indexes: 00 07 08 10 12 14
	 */

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(
		strstr(outbuffer, "Port: C0 (UCSI port 1), Supported:"));
	zassert_not_null(
		strstr(outbuffer, "Supported indexes: 00 07 08 10 12 14"));
}

ZTEST(ucsi_ppm_console, test_ppm_get_current_cam__bad_port)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_current_cam");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_current_cam 2");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_current_cam -1");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_get_current_cam__ucsi_fail)
{
	int rv;

	/* Report an error running the UCSI command */
	ppm_driver_mock_execute_cmd_sync_fake.return_val = -1;

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_current_cam 0");
	zassert_equal(rv, 1, "Expected %d, but got %d", 1, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

/**
 * @brief Custom fake to return a UCSI GET_CURRENT_CAM response
 */
static int execute_ucsi_get_current_cam(const struct device *dev,
					struct ucsi_control_t *cmd,
					uint8_t *out)
{
	zassert_equal(0, cmd->data_length, "GET_CURRENT_CAM length must be 0");
	zassert_equal(UCSI_GET_CURRENT_CAM, cmd->command,
		      "Command must be UCSI_GET_CURRENT_CAM");

	/* UCSI ports are 1-indexed */
	zassert_equal(1, cmd->command_specific[0] & 0x7F,
		      "Incorrect port sent");

	/* Report no active alt modes */
	out[0] = 0xFF;

	/* 1 byte written out */
	return 1;
}

ZTEST(ucsi_ppm_console, test_ppm_get_current_cam__success)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	ppm_driver_mock_execute_cmd_sync_fake.custom_fake =
		execute_ucsi_get_current_cam;

	rv = shell_execute_cmd(get_ec_shell(), "ppm get_current_cam 0");
	zassert_ok(rv, "Expected success (0), but got %d", rv);

	/* Inspect shell output. Expected output:
	 *
	 *    Port: C0 (UCSI port 1), CAM:
	 *    00000000: <hexdump>
	 */

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "Port: C0 (UCSI port 1), CAM:"));
	zassert_not_null(strstr(outbuffer, "00000000: ff  "));
	zassert_not_null(strstr(outbuffer, "No active alternate modes"));
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__bad_port)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam 2 0 0 enter");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam -1 0 0 enter");
	zassert_equal(rv, -ERANGE, "Expected %d, but got %d", -ERANGE, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__bad_new_cam)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam 0");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(),
			       "ppm set_new_cam 0 blah 0 enter");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__bad_am_specific)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam 0 0");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(),
			       "ppm set_new_cam 0 0 blah enter");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__bad_action)
{
	int rv;

	/* Note: emulated PPM driver returns 1 active port */

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam 0 0 0");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam 0 0 0 blah");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

#define SET_NEW_CAM_EXPECTED_VAL 0x55
#define SET_NEW_CAM_EXPECTED_AM_SPECIFIC 0x12345678

static uint8_t set_new_cam_expected_action = 0;

/**
 * @brief Custom fake to check a new alt mode entry or exit (UCSI_SET_NEW_CAM)
 */
static int execute_ucsi_set_new_cam(const struct device *dev,
				    struct ucsi_control_t *cmd, uint8_t *out)
{
	zassert_equal(0, cmd->data_length, "UCSI_SET_NEW_CAM length must be 0");
	zassert_equal(UCSI_SET_NEW_CAM, cmd->command,
		      "Command must be UCSI_SET_NEW_CAM");

	/* UCSI ports are 1-indexed */
	zassert_equal(1, cmd->command_specific[0] & 0x7F,
		      "Incorrect port sent");

	zassert_equal(set_new_cam_expected_action,
		      (cmd->command_specific[0] >> 7) & 0x01,
		      "Entry/exit bit incorrect");

	zassert_equal(SET_NEW_CAM_EXPECTED_VAL, cmd->command_specific[1],
		      "Wrong new CAM");

	zassert_equal(SET_NEW_CAM_EXPECTED_AM_SPECIFIC,
		      *((uint32_t *)&cmd->command_specific[2]),
		      "Wrong AM-specific data");

	/* No response */
	return 0;
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__ucsi_fail)
{
	int rv;

	/* Report an error running the UCSI command */
	ppm_driver_mock_execute_cmd_sync_fake.return_val = -1;

	rv = shell_execute_cmd(get_ec_shell(), "ppm set_new_cam 0 0 0 enter");
	zassert_equal(rv, 1, "Expected %d, but got %d", 1, rv);
	shell_backend_dummy_clear_output(get_ec_shell());
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__success_enter)
{
	int rv;

	set_new_cam_expected_action = 1; /* Entry */
	ppm_driver_mock_execute_cmd_sync_fake.custom_fake =
		execute_ucsi_set_new_cam;

	rv = shell_execute_cmd(
		get_ec_shell(),
		"ppm set_new_cam 0 " STRINGIFY(SET_NEW_CAM_EXPECTED_VAL) " " STRINGIFY(
			SET_NEW_CAM_EXPECTED_AM_SPECIFIC) " enter");
	zassert_ok(rv, "Expected success (0), but got %d", rv);
}

ZTEST(ucsi_ppm_console, test_ppm_set_new_cam__success_exit)
{
	int rv;

	set_new_cam_expected_action = 0; /* Exit */
	ppm_driver_mock_execute_cmd_sync_fake.custom_fake =
		execute_ucsi_set_new_cam;

	rv = shell_execute_cmd(
		get_ec_shell(),
		"ppm set_new_cam 0 " STRINGIFY(SET_NEW_CAM_EXPECTED_VAL) " " STRINGIFY(
			SET_NEW_CAM_EXPECTED_AM_SPECIFIC) " exit");
	zassert_ok(rv, "Expected success (0), but got %d", rv);
}
