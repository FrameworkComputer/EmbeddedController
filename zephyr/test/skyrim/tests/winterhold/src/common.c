/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(winterhold, CONFIG_SKYRIM_LOG_LEVEL);

ZTEST_SUITE(common, NULL, NULL, NULL, NULL, NULL);
