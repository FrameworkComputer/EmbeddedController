/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_keyscan

#include <assert.h>
#include <kernel.h>
#include <soc.h>

#include "keyboard_scan.h"

#if DT_NODE_EXISTS(DT_INST(0, cros_keyscan))

/* The keyboard matrix should have at least enough columns for the
 * standard keyboard with no keypad.
 */
BUILD_ASSERT(DT_INST_PROP_LEN(0, actual_key_mask) >= KEYBOARD_COLS_NO_KEYPAD);

/*
 * Override the default keyscan_config if the board defines a
 * cros-kb-raw-keyscan node.
 */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = DT_INST_PROP(0, output_settle),
	.debounce_down_us = DT_INST_PROP(0, debounce_down),
	.debounce_up_us = DT_INST_PROP(0, debounce_up),
	.scan_period_us = DT_INST_PROP(0, scan_period),
	.min_post_scan_delay_us = DT_INST_PROP(0, min_post_scan_delay),
	.poll_timeout_us = DT_INST_PROP(0, poll_timeout),
	.actual_key_mask = DT_INST_PROP(0, actual_key_mask),
};
#endif
