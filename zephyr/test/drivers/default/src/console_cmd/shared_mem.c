/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "shared_mem.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(console_cmd_shared_mem, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST_USER(console_cmd_shared_mem, test_shmem)
{
	char expected[32];

	snprintf(expected, sizeof(expected), "Size:%6d", shared_mem_size());

	CHECK_CONSOLE_CMD("shmem", expected, EC_SUCCESS);
}
