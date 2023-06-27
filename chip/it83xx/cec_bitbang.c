/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "cec_bitbang_chip.h"
#include "console.h"
#include "driver/cec/bitbang.h"
#include "gpio.h"
#include "hwtimer_chip.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_CEC_DEBUG
#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)
#else
#define CPRINTF(...)
#define CPRINTS(...)
#endif

/* Timestamp when the most recent interrupt occurred */
static timestamp_t interrupt_time;

/* Timestamp when the second most recent interrupt occurred */
static timestamp_t prev_interrupt_time;

/* Flag set when a transfer is initiated from the AP */
static bool transfer_initiated;

/*
 * ITE doesn't have a capture timer, so we use a countdown timer for timeout
 * events combined with a GPIO interrupt for capture events.
 */
void cec_tmr_cap_start(enum cec_cap_edge edge, int timeout)
{
	switch (edge) {
	case CEC_CAP_EDGE_NONE:
		gpio_disable_interrupt(CEC_GPIO_IN);
		break;
	case CEC_CAP_EDGE_FALLING:
		gpio_set_flags(CEC_GPIO_IN, GPIO_INT_FALLING);
		gpio_enable_interrupt(CEC_GPIO_IN);
		break;
	case CEC_CAP_EDGE_RISING:
		gpio_set_flags(CEC_GPIO_IN, GPIO_INT_RISING);
		gpio_enable_interrupt(CEC_GPIO_IN);
		break;
	}

	if (timeout > 0) {
		/*
		 * Take into account the delay from when the interrupt occurs to
		 * when we actually get here.
		 */
		int delay =
			CEC_US_TO_TICKS(get_time().val - interrupt_time.val);
		int timer_count = timeout - delay;

		/*
		 * Handle the case where the delay is greater than the timeout.
		 * This should never actually happen for typical delay and
		 * timeout values.
		 */
		if (timer_count < 0) {
			timer_count = 0;
			CPRINTS("CEC WARNING: timer_count < 0");
		}

		/* Start the timer and enable the timer interrupt */
		ext_timer_ms(CEC_EXT_TIMER, CEC_CLOCK_SOURCE, 1, 1, timer_count,
			     0, 1);
	} else {
		ext_timer_stop(CEC_EXT_TIMER, 1);
	}
}

void cec_tmr_cap_stop(void)
{
	gpio_disable_interrupt(CEC_GPIO_IN);
	ext_timer_stop(CEC_EXT_TIMER, 1);
}

int cec_tmr_cap_get(void)
{
	return CEC_US_TO_TICKS(interrupt_time.val - prev_interrupt_time.val);
}

__override void cec_update_interrupt_time(void)
{
	prev_interrupt_time = interrupt_time;
	interrupt_time = get_time();
}

void cec_ext_timer_interrupt(void)
{
	if (transfer_initiated) {
		transfer_initiated = false;
		cec_event_tx();
	} else {
		cec_update_interrupt_time();
		cec_event_timeout();
	}
}

void cec_gpio_interrupt(enum gpio_signal signal)
{
	cec_update_interrupt_time();
	cec_event_cap();
}

void cec_trigger_send(void)
{
	/* Elevate to interrupt context */
	transfer_initiated = true;
	task_trigger_irq(et_ctrl_regs[CEC_EXT_TIMER].irq);
}

void cec_enable_timer(void)
{
	/*
	 * Nothing to do. Interrupts will be enabled as needed by
	 * cec_tmr_cap_start().
	 */
}

void cec_disable_timer(void)
{
	cec_tmr_cap_stop();

	interrupt_time.val = 0;
	prev_interrupt_time.val = 0;
}

void cec_init_timer(void)
{
	ext_timer_ms(CEC_EXT_TIMER, CEC_CLOCK_SOURCE, 0, 0, 0, 1, 0);
}
