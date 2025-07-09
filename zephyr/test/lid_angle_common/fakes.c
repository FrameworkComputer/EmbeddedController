/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"

#include <zephyr/fff.h>

DEFINE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DEFINE_FAKE_VOID_FUNC(keyboard_scan_enable, int, enum kb_scan_disable_masks);
DEFINE_FAKE_VALUE_FUNC(int, tablet_get_mode);
