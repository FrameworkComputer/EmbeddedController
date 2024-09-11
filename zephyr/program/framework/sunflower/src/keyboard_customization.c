/* Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "hooks.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

static void disable_keyscan(void)
{
	/**
	 * We don't want to read the keyboard before turning on the power.
	 * Disable the keyboard scan function to avoid the watchdog.
	 */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_DISCONNECT);
}
DECLARE_HOOK(HOOK_INIT_EARLY, disable_keyscan, HOOK_PRIO_DEFAULT);

void board_caps_led_control(int data)
{
}

/*TODO: wait for the keyboard layout, matrix, and it8801 specification
 *to modify the correct mask.
 */
/* KSO mapping for discrete keyboard */
__override const uint8_t it8801_kso_mapping[] = {
	0, 1, 20, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16,
};
BUILD_ASSERT(ARRAY_SIZE(it8801_kso_mapping) == KEYBOARD_COLS_MAX);
