/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <zephyr/devicetree.h>

#define SCANCODE_SET2_IDX(nodelabel) DT_NODE_CHILD_IDX(DT_NODELABEL(nodelabel))

void set_scancode_set2_table(uint8_t idx);
