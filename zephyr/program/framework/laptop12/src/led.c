/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_adc.h"
#include "board_host_command.h"
#include "ec_commands.h"
#include "hooks.h"
#include "led.h"
#include "system.h"

#include "console.h"

static int fpr_led_brightness[FP_LED_BRIGHTNESS_COUNT] = {100, 80, 55, 40};
static int non_fpr_led_brightness[FP_LED_BRIGHTNESS_COUNT] = {55, 40, 15, 8};

#define FPR_POWER_BTN_BOARD_ID BOARD_VERSION_10

static void board_init_fp_led_duty(void)
{
	/* Duty is saved in bbram, so clear it first, before changing the duty levels  */
	system_set_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, 0);

	if (get_hardware_id(ADC_POWER_BUTTON_BOARD_ID) == FPR_POWER_BTN_BOARD_ID)
		fp_led_brightness_change(fpr_led_brightness);
	else
		fp_led_brightness_change(non_fpr_led_brightness);
}
DECLARE_HOOK(HOOK_INIT, board_init_fp_led_duty, HOOK_PRIO_DEFAULT - 1);
