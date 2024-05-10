/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "clock_s3.h"
#include "common.h"
#include "console.h"
#include "registers.h"
#include "scp_timer.h"
#include "scp_watchdog.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

#define CHECK_26M_PERIOD_US 50000
#define CHECK_CORE1_MAX_RETRY 3
#define NO_CORE1 0xFF

enum scp_sr_state {
	SR_S0,
	SR_S02S3,
	SR_S02S3_WAIT_C1,
	SR_S3,
};

#if SCP_CORE_SN == 0
static void irq_group11_handler(void)
{
	extern volatile int ec_int;

	task_set_event(TASK_ID_SR, TASK_EVENT_C1_READY);
	SCP_GIPC_IN_CLR = GIPC_IN(S3_IPI_READY);
	asm volatile("fence.i" ::: "memory");
	task_clear_pending_irq(ec_int);
}
DECLARE_IRQ(11, irq_group11_handler, 0);
#else
static void irq_group11_handler(void)
{
	extern volatile int ec_int;
	uint32_t sr_st;

	sr_st = SCP_GIPC_IN_SET;
	if (sr_st & GIPC_IN(S3_IPI_SUSPEND)) {
		CPRINTS("AP suspend");
		task_set_event(TASK_ID_SR, TASK_EVENT_SUSPEND);
		SCP_GIPC_IN_CLR = GIPC_IN(S3_IPI_SUSPEND);
	} else if (sr_st & GIPC_IN(S3_IPI_RESUME)) {
		CPRINTS("AP resume");
		SCP_GIPC_IN_CLR = GIPC_IN(S3_IPI_RESUME);
	}
	asm volatile("fence.i" ::: "memory");
	task_clear_pending_irq(ec_int);
}
DECLARE_IRQ(11, irq_group11_handler, 0);
#endif

#if SCP_CORE_SN == 0
void sr_task(void *u)
{
	enum scp_sr_state state = SR_S0;
	uint32_t event;
	uint32_t prev = 0, now = 0;
	uint32_t c1_retry = 0;

	task_enable_irq(SCP_IRQ_GIPC_IN2);

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
			/* wait 26M off */
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
					if (SCP_CORE1_RSTN_CLR &
					    SCP_CORE1_RUN) {
						/* alert core 1 to enter S3 */
						state = SR_S02S3_WAIT_C1;
						c1_retry = 0;
						SCP_GIPC_IN_SET =
							GIPC_IN(S3_IPI_SUSPEND);
					} else {
						state = SR_S3;
						c1_retry = NO_CORE1;
					}
				}
			}
			break;
		case SR_S02S3_WAIT_C1:
			/* wait core 1 ready */
			event = task_wait_event(CHECK_26M_PERIOD_US);
			if (event & TASK_EVENT_RESUME) {
				/* suspend is aborted */
				timer_disable(TIMER_SR);
				state = SR_S0;
				/* alert core 1 that core 0 resumed */
				SCP_GIPC_IN_SET = GIPC_IN(S3_IPI_RESUME);
			} else if (event & TASK_EVENT_C1_READY) {
				/* core 1 is ready */
				state = SR_S3;
			} else if (event & TASK_EVENT_TIMER) {
				c1_retry += 1;
				if (c1_retry >= CHECK_CORE1_MAX_RETRY)
					state = SR_S3;
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

			if (c1_retry < NO_CORE1) {
				/* alert core 1 that core 0 resumed */
				SCP_GIPC_IN_SET = GIPC_IN(S3_IPI_RESUME);
			}
			break;
		}
	}
}
#else
void sr_task(void *u)
{
	uint32_t event;

	task_enable_irq(SCP_IRQ_GIPC_IN3);

	while (1) {
		event = task_wait_event(-1);
		if (event & TASK_EVENT_SUSPEND) {
			interrupt_disable();
			watchdog_disable();

			/* alert core 0 that core 1 is ready */
			SCP_GIPC_IN_SET = GIPC_IN(S3_IPI_READY);

			/* wait core 0 resume */
			while ((SCP_GIPC_IN_SET & GIPC_IN(S3_IPI_RESUME)) == 0)
				;

			watchdog_enable();
			interrupt_enable();
		}
	}
}
#endif /* SCP_CORE_SN == 0 */
