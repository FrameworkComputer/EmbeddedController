/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_commands.h"
#include "touchpad.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VOID_FUNC(system_reset, int);
DECLARE_FAKE_VALUE_FUNC(int, system_run_image_copy, enum ec_image);
DECLARE_FAKE_VALUE_FUNC(int, touchpad_get_info, struct touchpad_info *);
DECLARE_FAKE_VALUE_FUNC(int, touchpad_debug, const uint8_t *, unsigned int,
			uint8_t **, unsigned int *);
DECLARE_FAKE_VALUE_FUNC(int, touchpad_update_write, int, int, const uint8_t *);

#define FFF_FAKES_LIST(FAKE)        \
	FAKE(system_reset)          \
	FAKE(system_run_image_copy) \
	FAKE(touchpad_get_info)     \
	FAKE(touchpad_debug)        \
	FAKE(touchpad_update_write)
