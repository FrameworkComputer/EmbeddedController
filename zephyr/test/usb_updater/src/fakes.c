/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "fakes.h"

DEFINE_FAKE_VOID_FUNC(system_reset, int);
DEFINE_FAKE_VALUE_FUNC(int, system_run_image_copy, enum ec_image);
DEFINE_FAKE_VALUE_FUNC(int, touchpad_get_info, struct touchpad_info *);
DEFINE_FAKE_VALUE_FUNC(int, touchpad_debug, const uint8_t *, unsigned int,
		       uint8_t **, unsigned int *);
DEFINE_FAKE_VALUE_FUNC(int, touchpad_update_write, int, int, const uint8_t *);
DEFINE_FAKE_VALUE_FUNC(enum ec_image, system_get_image_copy);
DEFINE_FAKE_VOID_FUNC(touchpad_task, void *);
DEFINE_FAKE_VALUE_FUNC(const char *, system_get_version, enum ec_image);
DEFINE_FAKE_VALUE_FUNC(enum rwsig_status, rwsig_get_status);
