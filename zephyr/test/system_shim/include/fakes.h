/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_SYSTEM_SHIM_INCLUDE_FAKES_H_
#define ZEPHYR_TEST_SYSTEM_SHIM_INCLUDE_FAKES_H_

#include <stdint.h>

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, cros_system_get_reset_cause);
DECLARE_FAKE_VALUE_FUNC(uint64_t, cros_system_deep_sleep_ticks);
DECLARE_FAKE_VALUE_FUNC(int, cros_system_hibernate, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC(const char *, cros_system_chip_vendor);
DECLARE_FAKE_VALUE_FUNC(const char *, cros_system_chip_name);
DECLARE_FAKE_VALUE_FUNC(const char *, cros_system_chip_revision);
DECLARE_FAKE_VALUE_FUNC(int, cros_system_soc_reset);
DECLARE_FAKE_VOID_FUNC(watchdog_reload);
DECLARE_FAKE_VOID_FUNC(board_hibernate);

#endif /* ZEPHYR_TEST_SYSTEM_SHIM_INCLUDE_FAKES_H_ */
