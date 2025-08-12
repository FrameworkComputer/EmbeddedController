/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(chip_enter_bootloader, uint8_t);

const static uint8_t bootloader_mode = 0x43;

void chip_enter_bootloader_custom(uint8_t mode)
{
	zassert_equal(mode, bootloader_mode);
}

ZTEST(hc_enter_bootloader, test_enter_bootloader_ok)
{
	struct ec_params_enter_bootloader params = {
		.mode = bootloader_mode,
	};

	system_is_locked_fake.return_val = 0;
	chip_enter_bootloader_fake.custom_fake = chip_enter_bootloader_custom;
	ec_cmd_enter_bootloader(NULL, &params);
	/* Wait for chip_enter_bootloader to execute. */
	k_msleep(15);
	zassert_equal(chip_enter_bootloader_fake.call_count, 1);
}

ZTEST(hc_enter_bootloader, test_enter_bootloader_system_locked)
{
	int ret;
	struct ec_params_enter_bootloader params = {
		.mode = bootloader_mode,
	};

	system_is_locked_fake.return_val = 1;
	ret = ec_cmd_enter_bootloader(NULL, &params);
	/* Wait for chip_enter_bootloader to execute. */
	k_msleep(15);
	zassert_equal(chip_enter_bootloader_fake.call_count, 0);
	zassert_equal(ret, EC_RES_ACCESS_DENIED);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	RESET_FAKE(chip_enter_bootloader);
}

ZTEST_SUITE(hc_enter_bootloader, drivers_predicate_post_main, NULL, reset,
	    reset, NULL);
