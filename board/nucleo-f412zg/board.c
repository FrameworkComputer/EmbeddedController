/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "util.h"

/**
 * Disable restricted commands when the system is locked.
 *
 * @see console.h system.c
 */
int console_is_restricted(void)
{
	return system_is_locked();
}

static void ap_deferred(void)
{
	/*
	 * in S3:   SLP_S3_L is 0 and SLP_S0_L is X.
	 * in S0ix: SLP_S3_L is X and SLP_S0_L is 0.
	 * in S0:   SLP_S3_L is 1 and SLP_S0_L is 1.
	 * in S5/G3, the FP MCU should not be running.
	 */
	int running = gpio_get_level(GPIO_PCH_SLP_S3_L)
			&& gpio_get_level(GPIO_PCH_SLP_S0_L);

	if (running) { /* S0 */
		disable_sleep(SLEEP_MASK_AP_RUN);
		hook_notify(HOOK_CHIPSET_RESUME);
	} else { /* S0ix/S3 */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		enable_sleep(SLEEP_MASK_AP_RUN);
	}
}
DECLARE_DEFERRED(ap_deferred);

/* PCH power state changes */
static void slp_event(enum gpio_signal signal)
{
	hook_call_deferred(&ap_deferred_data, 0);
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	/* Enable interrupt on PCH power signals */
	gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S0_L);
	/* enable the SPI slave interface if the PCH is up */
	hook_call_deferred(&ap_deferred_data, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
