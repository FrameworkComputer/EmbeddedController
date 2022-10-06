/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "test/drivers/test_state.h"
#include "test/drivers/test_mocks.h"

FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);

#define ARBITRARY_VERSION 0x1234

static int system_test_cbi_get_board_version(uint32_t *ver)
{
	*ver = ARBITRARY_VERSION;

	return 0;
}

ZTEST(system, test_system_get_board_version)
{
	RESET_FAKE(cbi_get_board_version);
	cbi_get_board_version_fake.custom_fake =
		system_test_cbi_get_board_version;

	zassert_equal(system_get_board_version(), ARBITRARY_VERSION);
}

ZTEST(system, test_system_get_board_version__bad_cbi_read)
{
	RESET_FAKE(cbi_get_board_version);
	cbi_get_board_version_fake.return_val = EC_ERROR_BUSY;

	zassert_equal(system_get_board_version(), -EC_ERROR_BUSY);
}
