/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "clock_s3.h"
#include "common.h"
#include "scp_timer.h"
#include "scp_watchdog.h"
#include "task.h"
#include "timer.h"

#define CHECK_26M_PERIOD_US 50000

enum scp_sr_state {
	SR_S0,
	SR_S02S3,
	SR_S3,
};

void sr_task(void *u)
{
	enum scp_sr_state state = SR_S0;
	uint32_t event;
	uint32_t prev, now;

	while (1) {
		switch (state) {
		case SR_S0:
			event = task_wait_event(-1);
			if (event & TASK_EVENT_SUSPEND) {
				timer_enable(TIMER_SR);
				prev = timer_read_raw_sr();
				state = SR_S02S3;
			}
			break;
		case SR_S02S3:
			event = task_wait_event(CHECK_26M_PERIOD_US);
			if (event & TASK_EVENT_RESUME) {
				/* suspend is aborted */
				timer_disable(TIMER_SR);
				state = SR_S0;
			} else if (event & TASK_EVENT_TIMER) {
				now = timer_read_raw_sr();
				if (now != prev) {
					/* 26M is still on */
					prev = now;
				} else {
					/* 26M is off */
					state = SR_S3;
				}
			}
			break;
		case SR_S3:
			interrupt_disable();
			watchdog_disable();

			/* change to 26M to stop core at here */
			clock_select_clock(SCP_CLK_SYSTEM);

			/* 26M is back */
			clock_select_clock(SCP_CLK_ULPOSC2_LOW_SPEED);

			watchdog_enable();
			interrupt_enable();
			timer_disable(TIMER_SR);
			state = SR_S0;
			break;
		}
	}
}
