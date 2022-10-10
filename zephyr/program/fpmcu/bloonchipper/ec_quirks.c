/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/timer/system_timer.h>

#define RESET_PERIPHERAL(idx) \
	reset_line_toggle_dt(&(struct reset_dt_spec)RESET_DT_SPEC_GET(idx));

extern void arm_core_mpu_disable(void);

static void prepare_for_sysjump_to_ec(void)
{
	/* Reset timers and UARTs to the defaults. */
	DT_FOREACH_STATUS_OKAY(st_stm32_timers, RESET_PERIPHERAL);
	DT_FOREACH_STATUS_OKAY(st_stm32_uart, RESET_PERIPHERAL);

	/*
	 * When HW_STACK_PROTECTION is enabled on ARMv7-M microcontroller then
	 * the last 64 bytes of the stack is protected to detect stack
	 * overflows. Size of protected region must be greater than exception
	 * frame, so CPU won't overwrite some other data when exception occurs.
	 *
	 * EC uses different RAM layout than Zephyr, so it's possible that after
	 * sysjump some variables are stored in the protected region. EC also
	 * reconfigures MPU late (Zephyr does it in reset handler).
	 *
	 * Disable MPU protection to avoid problems after sysjump to EC.
	 */
	arm_core_mpu_disable();

	/*
	 * Disable system clock (Cortex-M Systick it this case) because it's
	 * unused in EC.
	 */
	sys_clock_disable();
}
DECLARE_HOOK(HOOK_SYSJUMP, prepare_for_sysjump_to_ec, HOOK_PRIO_LAST);
