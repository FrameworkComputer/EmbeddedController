/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_SYSTEM_SHIM_INCLUDE_FAKES_H_
#define ZEPHYR_TEST_SYSTEM_SHIM_INCLUDE_FAKES_H_

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, cros_system_native_posix_get_reset_cause,
			const struct device *);
DECLARE_FAKE_VALUE_FUNC(uint64_t, cros_system_native_posix_deep_sleep_ticks,
			const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, cros_system_native_posix_hibernate,
			const struct device *, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC(const char *, cros_system_native_posix_get_chip_vendor,
			const struct device *);
DECLARE_FAKE_VALUE_FUNC(const char *, cros_system_native_posix_get_chip_name,
			const struct device *);
DECLARE_FAKE_VALUE_FUNC(const char *,
			cros_system_native_posix_get_chip_revision,
			const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, cros_system_native_posix_soc_reset,
			const struct device *);
DECLARE_FAKE_VOID_FUNC(watchdog_reload);
DECLARE_FAKE_VOID_FUNC(board_hibernate);

#endif /* ZEPHYR_TEST_SYSTEM_SHIM_INCLUDE_FAKES_H_ */
