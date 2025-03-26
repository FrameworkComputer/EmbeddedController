/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"
#include "throttle_ap.h"

/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_ACOK_OD,
	GPIO_GSC_EC_PWR_BTN_ODL,
	GPIO_LID_OPEN,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

#ifndef HAS_TASK_PROCHOT
static const struct prochot_cfg brya_prochot_cfg = {
	.gpio_prochot_in = GPIO_EC_PROCHOT_IN_L,
};

static void prochot_monitoring_init(void)
{
	/* Enable monitoring of the PROCHOT input to the EC */
	throttle_ap_config_prochot(&brya_prochot_cfg);
	gpio_enable_interrupt(GPIO_EC_PROCHOT_IN_L);
}
DECLARE_HOOK(HOOK_INIT, prochot_monitoring_init, HOOK_PRIO_DEFAULT);
#endif /* !HAS_TASK_PROCHOT */
