/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, board_vbus_source_enabled, int);
DECLARE_FAKE_VALUE_FUNC(int, ppc_discharge_vbus, int, int);
