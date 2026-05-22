/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fakes.h"

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FAKE_VALUE_FUNC(int, cros_system_get_reset_cause);
DEFINE_FAKE_VALUE_FUNC(uint64_t, cros_system_deep_sleep_ticks);
DEFINE_FAKE_VALUE_FUNC(int, cros_system_hibernate, uint32_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC(const char *, cros_system_chip_vendor);
DEFINE_FAKE_VALUE_FUNC(const char *, cros_system_chip_name);
DEFINE_FAKE_VALUE_FUNC(const char *, cros_system_chip_revision);
DEFINE_FAKE_VALUE_FUNC(int, cros_system_soc_reset);
DEFINE_FAKE_VOID_FUNC(watchdog_reload);
DEFINE_FAKE_VOID_FUNC(board_hibernate);

static void system_before_after(void *test_data)
{
#ifdef CONFIG_PLATFORM_EC_BBRAM_TYPE_BBRAM
	const struct device *bbram_dev =
		DEVICE_DT_GET_OR_NULL(DT_CHOSEN(cros_ec_bbram));

	if (bbram_dev != NULL) {
		bbram_emul_set_invalid(bbram_dev, false);
	}
#endif

	RESET_FAKE(cros_system_get_reset_cause);
	RESET_FAKE(cros_system_deep_sleep_ticks);
	RESET_FAKE(cros_system_hibernate);
	RESET_FAKE(cros_system_chip_vendor);
	RESET_FAKE(cros_system_chip_name);
	RESET_FAKE(cros_system_chip_revision);
	RESET_FAKE(cros_system_soc_reset);
	RESET_FAKE(watchdog_reload);
	RESET_FAKE(board_hibernate);
}

ZTEST_SUITE(system, NULL, NULL, system_before_after, system_before_after, NULL);
