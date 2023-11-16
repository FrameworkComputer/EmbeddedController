/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <rollback.h>
#include <rollback_private.h>

struct rollback_data rollback_initial_data = {
	.id = 0,
	.rollback_min_version = CONFIG_PLATFORM_EC_ROLLBACK_VERSION,
#ifdef CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE
	.secret = { 0 },
#endif
	.cookie = CROS_EC_ROLLBACK_COOKIE,
};

const int32_t rollback_version __attribute__((section(".rw_rbver"))) =
	CONFIG_PLATFORM_EC_ROLLBACK_VERSION;
