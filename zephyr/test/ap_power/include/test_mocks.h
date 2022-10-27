/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TEST_AP_POWER_TEST_MOCKS_H
#define __TEST_AP_POWER_TEST_MOCKS_H

#include <zephyr/fff.h>

/*
 * Mock declarations
 */

/* Mocks for common/extpower_gpio.c */
DECLARE_FAKE_VALUE_FUNC(int, extpower_is_present);

/* Mocks for common/system.c */
DECLARE_FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);

#endif /* __TEST_AP_POWER_TEST_MOCKS_H */
