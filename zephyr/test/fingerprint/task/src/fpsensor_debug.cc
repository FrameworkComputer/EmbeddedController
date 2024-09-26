/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "console.h"
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_utils.h>
#include <mkbp_event.h>
#include <rollback.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_debug, NULL, NULL, NULL, NULL, NULL);

ZTEST(fpsensor_debug, test_console_fpinfo)
{
	char console_input[] = "fpinfo";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);
}
