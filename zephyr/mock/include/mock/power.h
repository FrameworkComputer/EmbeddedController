/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_MOCK_POWER_H
#define ZEPHYR_TEST_MOCK_POWER_H

#include <zephyr/fff.h>
#include "power.h"

/* Mocks for ec/power/common.c and board specific implementations */
DECLARE_FAKE_VALUE_FUNC(enum power_state, power_handle_state, enum power_state);
DECLARE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DECLARE_FAKE_VOID_FUNC(chipset_power_on);
DECLARE_FAKE_VALUE_FUNC(int, command_power, int, const char **);

enum power_state power_handle_state_custom_fake(enum power_state state);

void chipset_force_shutdown_custom_fake(enum chipset_shutdown_reason reason);

void chipset_power_on_custom_fake(void);

int command_power_custom_fake(int argc, const char **argv);

#endif /* ZEPHYR_TEST_MOCK_POWER_H */
