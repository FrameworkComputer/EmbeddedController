/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* tiny substitute of the runtime layer */

#include "atomic.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "cpu.h"
#include "debug_printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

volatile uint32_t last_event;
atomic_t sleep_mask;

/* High word of the 64-bit timestamp counter  */
static volatile uint32_t clksrc_high;

timestamp_t get_time(void)
{
	timestamp_t t;

	t.le.lo = STM32_TIM32_CNT(2);
	t.le.hi = clksrc_high;
	return t;
}

void force_time(timestamp_t ts)
{
	STM32_TIM32_CNT(2) = ts.le.lo;
}

void udelay(unsigned int us)
{
	unsigned int t0 = STM32_TIM32_CNT(2);
	while ((STM32_TIM32_CNT(2) - t0) < us)
		;
}

void task_enable_irq(int irq)
{
	CPU_NVIC_EN(0) = 1 << irq;
}

void task_disable_irq(int irq)
{
	CPU_NVIC_DIS(0) = 1 << irq;
}

void task_clear_pending_irq(int irq)
{
	CPU_NVIC_UNPEND(0) = 1 << irq;
}

void interrupt_disable(void)
{
	asm("cpsid i");
}

void interrupt_enable(void)
{
	asm("cpsie i");
}

void task_set_event(task_id_t tskid, uint32_t event)
{
	last_event = event;
}

static void tim2_interrupt(void)
{
	uint32_t stat = STM32_TIM_SR(2);

	if (stat & 2) { /* Event match */
		/* disable match interrupt but keep update interrupt */
		STM32_TIM_DIER(2) = 1;
		last_event = TASK_EVENT_TIMER;
	}
	if (stat & 1) /* Counter overflow */
		clksrc_high++;

	STM32_TIM_SR(2) = ~stat & 3; /* clear interrupt flags */
	task_clear_pending_irq(STM32_IRQ_TIM2);
}
DECLARE_IRQ(STM32_IRQ_TIM2, tim2_interrupt, 1);

static void zinger_config_hispeed_clock(void)
{
	/* Ensure that HSI8 is ON */
	if (!(STM32_RCC_CR & BIT(1))) {
		/* Enable HSI */
		STM32_RCC_CR |= BIT(0);
		/* Wait for HSI to be ready */
		while (!(STM32_RCC_CR & BIT(1)))
			;
	}
	/* PLLSRC = HSI, PLLMUL = x12 (x HSI/2) = 48Mhz */
	STM32_RCC_CFGR = 0x00288000;
	/* Enable PLL */
	STM32_RCC_CR |= BIT(24);
	/* Wait for PLL to be ready */
	while (!(STM32_RCC_CR & BIT(25)))
		;

	/* switch SYSCLK to PLL */
	STM32_RCC_CFGR = 0x00288002;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
}

void runtime_init(void)
{
	/*
	 * put 1 Wait-State for flash access to ensure proper reads at 48Mhz
	 * and enable prefetch buffer.
	 */
	STM32_FLASH_ACR = STM32_FLASH_ACR_LATENCY | STM32_FLASH_ACR_PRFTEN;

	config_hispeed_clock();

	rtc_init();
}

/*
 * minimum delay to enter stop mode
 * STOP_MODE_LATENCY: max time to wake up from STOP mode with regulator in low
 * power mode is 5 us + PLL locking time is 200us.
 * SET_RTC_MATCH_DELAY: max time to set RTC match alarm. if we set the alarm
 * in the past, it will never wake up and cause a watchdog.
 */
#define STOP_MODE_LATENCY 300 /* us */
#define SET_RTC_MATCH_DELAY 200 /* us */
#define MAX_LATENCY (STOP_MODE_LATENCY + SET_RTC_MATCH_DELAY)

uint32_t task_wait_event(int timeout_us)
{
	uint32_t evt;
	timestamp_t t0, t1;
	struct rtc_time_reg rtc0, rtc1;

	t1.val = get_time().val + timeout_us;

	interrupt_disable();
	/* the event already happened */
	if (last_event || !timeout_us) {
		evt = last_event;
		last_event = 0;

		asm volatile("cpsie i ; isb");
		return evt;
	}

	/* loop until an event is triggered */
	while (1) {
		/* set timeout on timer */
		if (timeout_us < 0) {
			asm volatile("wfi");
		} else if (timeout_us <= MAX_LATENCY ||
			   t1.le.lo - timeout_us > t1.le.lo + MAX_LATENCY ||
			   !DEEP_SLEEP_ALLOWED) {
			STM32_TIM32_CCR1(2) = STM32_TIM32_CNT(2) + timeout_us;
			STM32_TIM_DIER(2) = 3; /*  match interrupt and UIE */

			asm volatile("wfi");

			STM32_TIM_DIER(2) = 1; /* disable match, keep UIE */
		} else {
			t0 = get_time();

			/* set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			set_rtc_alarm(0, timeout_us - STOP_MODE_LATENCY, &rtc0,
				      0);

			asm volatile("wfi");

			CPU_SCB_SYSCTRL &= ~0x4;

			zinger_config_hispeed_clock();

			/* fast forward timer according to RTC counter */
			reset_rtc_alarm(&rtc1);
			t0.val += get_rtc_diff(&rtc0, &rtc1);
			force_time(t0);
		}

		asm volatile("cpsie i ; isb");
		/* note: interrupt that woke us up will run here */
		interrupt_disable();

		t0 = get_time();
		/* check for timeout if timeout was set */
		if (timeout_us >= 0 && t0.val >= t1.val)
			last_event = TASK_EVENT_TIMER;
		/* break from loop when event has triggered */
		if (last_event)
			break;
		/* recalculate timeout if timeout was set */
		if (timeout_us >= 0)
			timeout_us = t1.val - t0.val;
	}

	evt = last_event;
	last_event = 0;
	asm volatile("cpsie i ; isb");
	return evt;
}

uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us)
{
	uint32_t evt = 0;

	/* Add the timer event to the mask so we can indicate a timeout */
	event_mask |= TASK_EVENT_TIMER;

	/* Wait until an event matching event_mask */
	do {
		evt |= task_wait_event(timeout_us);
	} while (!(evt & event_mask));

	/* Restore any pending events not in the event_mask */
	if (evt & ~event_mask)
		task_set_event(0, evt & ~event_mask);

	return evt & event_mask;
}

noreturn void __keep cpu_reset(void)
{
	/* Disable interrupts */
	interrupt_disable();
	/* reboot the CPU */
	CPU_NVIC_APINT = 0x05fa0004;
	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

void system_reset(int flags)
{
	cpu_reset();
}
/**
 * Default exception handler, which reports a panic.
 *
 * Declare this as a naked call so we can extract the real LR and SP.
 */
void exception_panic(void) __attribute__((naked));
void exception_panic(void)
{
	asm volatile(
#ifdef CONFIG_DEBUG_PRINTF
		"mov r0, %0\n"
		/* TODO: Should this be SP_process instead of SP_main? */
		"mov r3, sp\n"
		"ldr r1, [r3, #6*4]\n" /* retrieve exception PC */
		"ldr r2, [r3, #5*4]\n" /* retrieve exception LR */
		"bl debug_printf\n"
#endif
		"bl cpu_reset\n"
		:
		: "r"("PANIC PC=%08x LR=%08x\n\n"));
}

void panic_reboot(void)
{ /* for div / 0 */
	debug_printf("DIV0 PANIC\n\n");
	cpu_reset();
}

enum ec_image system_get_image_copy(void)
{
	if (is_ro_mode())
		return EC_IMAGE_RO;
	else
		return EC_IMAGE_RW;
}

/* --- stubs --- */
void __hw_timer_enable_clock(int n, int enable)
{ /* Done in hardware init */
}

void usleep(unsigned int us)
{ /* Used only as a workaround */
}
