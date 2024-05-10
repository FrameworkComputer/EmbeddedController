/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "mock/fpsensor_detect_mock.h"
#include "string.h"
#include "test_util.h"

#include <stdbool.h>
#include <stddef.h>

static const struct ec_response_get_protocol_info expected_info[] = {
	[FP_TRANSPORT_TYPE_SPI] = {
		.flags = 1,
		.max_response_packet_size = 544,
		.max_request_packet_size = 544,
		.protocol_versions = 8,
	},
	[FP_TRANSPORT_TYPE_UART] = {
		.flags = 1,
		.max_response_packet_size = 256,
		.max_request_packet_size = 544,
		.protocol_versions = 8,
	}
};

test_static int test_host_command_protocol_info(
	enum fp_transport_type transport_type,
	const struct ec_response_get_protocol_info *expected)
{
	struct ec_response_get_protocol_info info;
	int rv;

	mock_ctrl_fpsensor_detect.fpsensor_detect_get_type_return =
		FP_SENSOR_TYPE_FPC;
	mock_ctrl_fpsensor_detect.get_fp_transport_type_return = transport_type;

	rv = test_send_host_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0, &info,
				    sizeof(info));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(info.flags, expected->flags, "%d");
	TEST_EQ(info.max_request_packet_size, expected->max_request_packet_size,
		"%d");
	TEST_EQ(info.max_response_packet_size,
		expected->max_response_packet_size, "%d");
	TEST_EQ(info.protocol_versions, expected->protocol_versions, "%d");

	return EC_SUCCESS;
}

test_static int test_host_command_protocol_info_uart(void)
{
	return test_host_command_protocol_info(
		FP_TRANSPORT_TYPE_UART, &expected_info[FP_TRANSPORT_TYPE_UART]);
}

test_static int test_host_command_protocol_info_spi(void)
{
	return test_host_command_protocol_info(
		FP_TRANSPORT_TYPE_SPI, &expected_info[FP_TRANSPORT_TYPE_SPI]);
}

void run_test(int argc, const char **argv)
{
	/* The tests after this only work on device right now. */
	if (IS_ENABLED(EMU_BUILD)) {
		test_print_result();
		return;
	}

	if (argc < 2) {
		ccprintf("usage: runtest [uart|spi]\n");
		test_fail();
		return;
	}

	/* The transport type is cached in a static variable, so the tests
	 * cannot be run back to back (without reboot).
	 */
	if (strncmp(argv[1], "uart", 4) == 0 && IS_ENABLED(BOARD_BLOONCHIPPER))
		RUN_TEST(test_host_command_protocol_info_uart);
	else if (strncmp(argv[1], "spi", 3) == 0)
		RUN_TEST(test_host_command_protocol_info_spi);

	test_print_result();
}
