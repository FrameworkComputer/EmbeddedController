/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "fakes.h"

DEFINE_FAKE_VALUE_FUNC(int, cros_system_native_posix_get_reset_cause,
		       const struct device *);
DEFINE_FAKE_VALUE_FUNC(uint64_t, cros_system_native_posix_deep_sleep_ticks,
		       const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, cros_system_native_posix_hibernate,
		       const struct device *, uint32_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC(const char *, cros_system_native_posix_get_chip_vendor,
		       const struct device *);
DEFINE_FAKE_VALUE_FUNC(const char *, cros_system_native_posix_get_chip_name,
		       const struct device *);
DEFINE_FAKE_VALUE_FUNC(const char *, cros_system_native_posix_get_chip_revision,
		       const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, cros_system_native_posix_soc_reset,
		       const struct device *);
DEFINE_FAKE_VOID_FUNC(watchdog_reload);
DEFINE_FAKE_VOID_FUNC(board_hibernate);

static void system_before_after(void *test_data)
{
	const struct device *bbram_dev =
		DEVICE_DT_GET_OR_NULL(DT_CHOSEN(cros_ec_bbram));

	RESET_FAKE(cros_system_native_posix_get_reset_cause);
	RESET_FAKE(cros_system_native_posix_deep_sleep_ticks);
	RESET_FAKE(cros_system_native_posix_hibernate);
	RESET_FAKE(cros_system_native_posix_get_chip_vendor);
	RESET_FAKE(cros_system_native_posix_get_chip_name);
	RESET_FAKE(cros_system_native_posix_get_chip_revision);
	RESET_FAKE(cros_system_native_posix_soc_reset);
	RESET_FAKE(watchdog_reload);
	RESET_FAKE(board_hibernate);

	if (bbram_dev != NULL) {
		bbram_emul_set_invalid(bbram_dev, false);
	}
}

ZTEST_SUITE(system, NULL, NULL, system_before_after, system_before_after, NULL);
