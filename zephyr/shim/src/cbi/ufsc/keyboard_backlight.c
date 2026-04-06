/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "hooks.h"
#include "keyboard_backlight.h"
#include "system.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cros_cbi_ufsc_kblight, LOG_LEVEL_INF);

#define POLICY_NODE DT_NODELABEL(kb_backlight_policy)

static bool is_kblight_present = true;

static void kb_backlight_ufsc_init(void)
{
	if (cros_cbi_ufsc_check_match(
		    CBI_UFSC_VALUE_ID(DT_PHANDLE(POLICY_NODE, enable_value)))) {
		LOG_INF("KB Backlight: Enabled via UFSC Policy");
		is_kblight_present = true;
	} else {
		LOG_INF("KB Backlight: Disabled (UFSC mismatch)");
		is_kblight_present = false;
		kblight_enable(0);
	}
}
DECLARE_HOOK(HOOK_INIT, kb_backlight_ufsc_init, HOOK_PRIO_POST_I2C);

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	if (!is_kblight_present) {
		return flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB);
	}
	return flags0;
}
