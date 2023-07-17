/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/sys/util.h>

enum kb_scan_disable_masks {
	KB_SCAN_DISABLE_A = BIT(0),
	KB_SCAN_DISABLE_B = BIT(1),
};

void keyboard_scan_enable(int enable, enum kb_scan_disable_masks mask);
