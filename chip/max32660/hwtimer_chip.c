/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 HW Timer module for Chrome EC */

#include "clock.h"
#include "console.h"
#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "task.h"
#include "timer.h"
#include "registers.h"
#include "tmr_regs.h"
#include "gcr_regs.h"

/* Define the rollover timer */
#define TMR_ROLLOVER MXC_TMR0
#define TMR_ROLLOVER_IRQ EC_TMR0_IRQn

/* Define the event timer */
#define TMR_EVENT MXC_TMR1
#define TMR_EVENT_IRQ EC_TMR1_IRQn

#define ROLLOVER_EVENT 1
#define NOT_ROLLOVER_EVENT 0

#define TMR_PRESCALER MXC_V_TMR_CN_PRES_DIV8
#define TMR_DIV (1 << TMR_PRESCALER)

/* The frequency of timer using the prescaler */
#define TIMER_FREQ_HZ (PeripheralClock / TMR_DIV)

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

static uint32_t last_deadline;

/* brief Timer prescaler values */
enum tmr_pres {
	TMR_PRES_1 = MXC_V_TMR_CN_PRES_DIV1,     /// Divide input clock by 1
	TMR_PRES_2 = MXC_V_TMR_CN_PRES_DIV2,     /// Divide input clock by 2
	TMR_PRES_4 = MXC_V_TMR_CN_PRES_DIV4,     /// Divide input clock by 4
	TMR_PRES_8 = MXC_V_TMR_CN_PRES_DIV8,     /// Divide input clock by 8
	TMR_PRES_16 = MXC_V_TMR_CN_PRES_DIV16,   /// Divide input clock by 16
	TMR_PRES_32 = MXC_V_TMR_CN_PRES_DIV32,   /// Divide input clock by 32
	TMR_PRES_64 = MXC_V_TMR_CN_PRES_DIV64,   /// Divide input clock by 64
	TMR_PRES_128 = MXC_V_TMR_CN_PRES_DIV128, /// Divide input clock by 128
	TMR_PRES_256 =
		(0x20 << MXC_F_TMR_CN_PRES_POS), /// Divide input clock by 256
	TMR_PRES_512 =
		(0x21 << MXC_F_TMR_CN_PRES_POS), /// Divide input clock by 512
	TMR_PRES_1024 =
		(0x22 << MXC_F_TMR_CN_PRES_POS), /// Divide input clock by 1024
	TMR_PRES_2048 =
		(0x23 << MXC_F_TMR_CN_PRES_POS), /// Divide input clock by 2048
	TMR_PRES_4096 =
		(0x24 << MXC_F_TMR_CN_PRES_POS), /// Divide input clock by 4096
};

/* Timer modes */
enum tmr_mode {
	TMR_MODE_ONESHOT = MXC_V_TMR_CN_TMODE_ONESHOT, /// Timer Mode ONESHOT
	TMR_MODE_CONTINUOUS =
		MXC_V_TMR_CN_TMODE_CONTINUOUS,	 /// Timer Mode CONTINUOUS
	TMR_MODE_COUNTER = MXC_V_TMR_CN_TMODE_COUNTER, /// Timer Mode COUNTER
	TMR_MODE_PWM = MXC_V_TMR_CN_TMODE_PWM,	 /// Timer Mode PWM
	TMR_MODE_CAPTURE = MXC_V_TMR_CN_TMODE_CAPTURE, /// Timer Mode CAPTURE
	TMR_MODE_COMPARE = MXC_V_TMR_CN_TMODE_COMPARE, /// Timer Mode COMPARE
	TMR_MODE_GATED = MXC_V_TMR_CN_TMODE_GATED,     /// Timer Mode GATED
	TMR_MODE_CAPTURE_COMPARE =
		MXC_V_TMR_CN_TMODE_CAPTURECOMPARE /// Timer Mode CAPTURECOMPARE
};

/*
 * Calculate the number of microseconds for a given timer tick
 */
static inline uint32_t ticks_to_usecs(uint32_t ticks)
{
	return (uint64_t)ticks * SECOND / TIMER_FREQ_HZ;
}

/*
 * Calculate the number of timer ticks for a given microsecond value
 */
static inline uint32_t usecs_to_ticks(uint32_t usecs)
{
	return ((uint64_t)(usecs)*TIMER_FREQ_HZ / SECOND);
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t event_time_us;
	uint32_t event_time_ticks;
	uint32_t time_now;

	last_deadline = deadline;
	time_now = __hw_clock_source_read();

	/* check if the deadline has rolled over */
	if (deadline < time_now) {
		event_time_us = (0xFFFFFFFF - time_now) + deadline;
	} else {
		/* How long from the current time to the deadline? */
		event_time_us = (deadline - __hw_clock_source_read());
	}

	/* Convert event_time to ticks rounding up */
	event_time_ticks = usecs_to_ticks(event_time_us) + 1;

	/* set the event time into the timer compare */
	TMR_EVENT->cmp = event_time_ticks;
	/* zero out the timer */
	TMR_EVENT->cnt = 0;
	TMR_EVENT->cn |= MXC_F_TMR_CN_TEN;
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	TMR_EVENT->cn &= ~(MXC_F_TMR_CN_TEN);
}

uint32_t __hw_clock_source_read(void)
{
	uint32_t timer_count_ticks;

	/* Read the timer value and return the results in microseconds */
	timer_count_ticks = TMR_ROLLOVER->cnt;
	return ticks_to_usecs(timer_count_ticks);
}

void __hw_clock_source_set(uint32_t ts)
{
	uint32_t timer_count_ticks;
	timer_count_ticks = usecs_to_ticks(ts);
	TMR_ROLLOVER->cnt = timer_count_ticks;
}

/**
 * Interrupt handler for Timer
 */
static void __timer_event_isr(void)
{
	/* Clear the event timer */
	TMR_EVENT->intr = MXC_F_TMR_INTR_IRQ_CLR;
	/* Process the timer, pass in that this was NOT a rollover event */
	if (TMR_ROLLOVER->intr) {
		TMR_ROLLOVER->intr = MXC_F_TMR_INTR_IRQ_CLR;
		process_timers(ROLLOVER_EVENT);
	} else {
		process_timers(NOT_ROLLOVER_EVENT);
	}
}
/*
 * Declare the EC Timer lower in priority than the I2C interrupt. This
 * allows the I2C driver to process time sensitive interrupts.
 */
DECLARE_IRQ(EC_TMR1_IRQn, __timer_event_isr, 2);

static void init_timer(mxc_tmr_regs_t *timer, enum tmr_pres prescaler,
		       enum tmr_mode mode, uint32_t count)
{
	/* Disable the Timer */
	timer->cn &= ~(MXC_F_TMR_CN_TEN);

	if (timer == MXC_TMR0) {
		/* Enable Timer 0 Clock */
		MXC_GCR->perckcn0 &= ~(MXC_F_GCR_PERCKCN0_T0D);
	} else if (timer == MXC_TMR1) {
		/* Enable Timer 1 Clock */
		MXC_GCR->perckcn0 &= ~(MXC_F_GCR_PERCKCN0_T1D);
	} else if (timer == MXC_TMR2) {
		/* Enable Timer 2 Clock */
		MXC_GCR->perckcn0 &= ~(MXC_F_GCR_PERCKCN0_T2D);
	}

	/* Disable timer and clear settings */
	timer->cn = 0;

	/* Clear interrupt flag */
	timer->intr = MXC_F_TMR_INTR_IRQ_CLR;

	/* Set the prescaler */
	timer->cn = (prescaler << MXC_F_TMR_CN_PRES_POS);

	/* Configure the timer */
	timer->cn = (timer->cn & ~(MXC_F_TMR_CN_TMODE | MXC_F_TMR_CN_TPOL)) |
		    ((mode << MXC_F_TMR_CN_TMODE_POS) & MXC_F_TMR_CN_TMODE) |
		    ((0 << MXC_F_TMR_CN_TPOL_POS) & MXC_F_TMR_CN_TPOL);

	timer->cnt = 0x1;
	timer->cmp = count;
}

int __hw_clock_source_init(uint32_t start_t)
{
	/* Initialize two timers, one for the OS Rollover and one for the OS
	 * Events */
	init_timer(TMR_ROLLOVER, TMR_PRESCALER, TMR_MODE_CONTINUOUS,
		   0xffffffff);
	init_timer(TMR_EVENT, TMR_PRESCALER, TMR_MODE_COMPARE, 0x0);
	__hw_clock_source_set(start_t);

	/* Enable the timers */
	TMR_ROLLOVER->cn |= MXC_F_TMR_CN_TEN;
	TMR_EVENT->cn |= MXC_F_TMR_CN_TEN;

	/* Enable the IRQ */
	task_enable_irq(TMR_EVENT_IRQ);

	/* Return the Event timer IRQ number (NOT the Rollover IRQ) */
	return TMR_EVENT_IRQ;
}

static int hwtimer_display(int argc, char **argv)
{
	CPRINTS(" TMR_EVENT count 0x%08x", TMR_EVENT->cnt);
	CPRINTS(" TMR_ROLLOVER count 0x%08x", TMR_ROLLOVER->cnt);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hwtimer, hwtimer_display, "hwtimer",
			"Display hwtimer counts");
