/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_SUITE(console_cmd_switch, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST_USER(console_cmd_switch, test_mmapinfo)
{
	uint8_t *memmap_switches = host_get_memmap(EC_MEMMAP_SWITCHES);
	uint8_t before = *memmap_switches;
	char expected[32];

	*memmap_switches = 0x3;
	snprintf(expected, sizeof(expected), "memmap switches = 0x%x",
		 *memmap_switches);

	CHECK_CONSOLE_CMD("mmapinfo", expected, EC_SUCCESS);
	CHECK_CONSOLE_CMD("mmapinfo", "lid_open", EC_SUCCESS);
	CHECK_CONSOLE_CMD("mmapinfo", "powerbtn", EC_SUCCESS);

	*memmap_switches = before;
}
