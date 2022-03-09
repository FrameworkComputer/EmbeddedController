/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "charge_manager.h"
#include "console.h"
#include "test_state.h"

ZTEST_SUITE(console_cmd_charge_manager, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

/**
 * Test the chgsup (charge supplier info) command. This command only prints to
 * console some information which is not yet possible to verify. So just check
 * that the console command ran successfully.
 */
ZTEST_USER(console_cmd_charge_manager, test_chgsup)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgsup"), NULL);
}
