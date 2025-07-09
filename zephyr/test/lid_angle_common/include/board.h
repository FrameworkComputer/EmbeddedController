/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file Define the mocks for this test.
 */

#ifndef __ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_BOARD_H
#define __ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_BOARD_H

#include "chipset.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DECLARE_FAKE_VOID_FUNC(keyboard_scan_enable, int, enum kb_scan_disable_masks);
DECLARE_FAKE_VALUE_FUNC(int, tablet_get_mode);

#endif /* __ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_BOARD_H */
