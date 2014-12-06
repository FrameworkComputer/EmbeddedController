/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

#define OSC_CLK         48000000  /* Default is 40MHz (target is 48MHz) */
#define WAKE_INTERVAL   61        /* Unit: 61 usec */
#define IDLE_PARAMS     0x7       /* Support deep idle, instant wake-up */

/*
 * Frequency multiplier values definition according to the requested
 * PLL_CLOCK Clock Frequency
 */
#define HFCGN    0x02
#if   (OSC_CLK == 50000000)
#define HFCGMH   0x0B
#define HFCGML   0xEC
#elif (OSC_CLK == 48000000)
#define HFCGMH   0x0B
#define HFCGML   0x72
#elif (OSC_CLK == 40000000)
#define HFCGMH   0x09
#define HFCGML   0x89
#elif (OSC_CLK == 33000000)
#define HFCGMH   0x07
#define HFCGML   0xDE
#else
#error "Unsupported FMCLK Clock Frequency"
#endif

/* Low power idle statistics */
#ifdef CONFIG_LOW_POWER_IDLE
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
/*
 * Fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the low speed clock is not used.
 */
#define CONSOLE_IN_USE_ON_BOOT_TIME (15*SECOND)
static int console_in_use_timeout_sec = 15;
static timestamp_t console_expire_time;
#endif


static int freq;

/* Low power idle statistics */

/**
 * Enable clock to peripheral by setting the CGC register pertaining
 * to run, sleep, and/or deep sleep modes.
 *
 * @param offset  Offset of the peripheral. See enum clock_gate_offsets.
 * @param mask    Bit mask of the bits within CGC reg to set.
 * @param mode    no used
 */
void clock_enable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	/* Don't support for different mode */
	uint8_t reg_mask = mask & 0xff;

	/* Set PD bit to 0 */
	NPCX_PWDWN_CTL(offset) &= ~reg_mask;
	/* Wait for clock change to take affect. */
	clock_wait_cycles(3);
}

/**
 * Disable clock to peripheral by setting the CGC register pertaining
 * to run, sleep, and/or deep sleep modes.
 *
 * @param offset  Offset of the peripheral. See enum clock_gate_offsets.
 * @param mask    Bit mask of the bits within CGC reg to clear.
 * @param mode    no used
 */
void clock_disable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	/* Don't support for different mode */
	uint8_t reg_mask = mask & 0xff;

	/* Set PD bit to 1 */
	NPCX_PWDWN_CTL(offset) |= reg_mask;

}

/*****************************************************************************/
/* IC specific low-level driver */

/**
 * Set the CPU clocks and PLLs.
 */
void clock_init(void)
{
	/*
	 * Configure Frequency multiplier values according to the requested
	 * FMCLK Clock Frequency
	 */
	NPCX_HFCGN  = HFCGN;
	NPCX_HFCGML = HFCGML;
	NPCX_HFCGMH = HFCGMH;

	/* Load M and N values into the frequency multiplier */
	SET_BIT(NPCX_HFCGCTRL, NPCX_HFCGCTRL_LOAD);

	/* Wait for stable */
	while (IS_BIT_SET(NPCX_HFCGCTRL, NPCX_HFCGCTRL_CLK_CHNG))
		;

	/* Keep Core CLK & FMCLK are the same */
	NPCX_HFCGP = 0x00;

	freq = OSC_CLK;

	/* Notify modules of frequency change */
	hook_notify(HOOK_FREQ_CHANGE);
}

/**
 * Return the current clock frequency in Hz.
 */
int clock_get_freq(void)
{
	return freq;
}

/**
 * Return the current APB1 clock frequency in Hz.
 */
int clock_get_apb1_freq(void)
{
	int apb1_div = (NPCX_HFCBCD & 0x03) + 1;
	return freq/apb1_div;
}

/**
 * Return the current APB2 clock frequency in Hz.
 */
int clock_get_apb2_freq(void)
{
	int apb2_div = ((NPCX_HFCBCD>>2) & 0x03) + 1;
	return freq/apb2_div;
}

/**
 * Wait for a number of clock cycles.
 *
 * Simple busy waiting for use before clocks/timers are initialized.
 *
 * @param cycles	Number of cycles to wait.
 */
void clock_wait_cycles(uint32_t cycles)
{
	asm("1: subs %0, #1\n"
	    "   bne 1b\n" :: "r"(cycles));
}

#ifdef CONFIG_LOW_POWER_IDLE
void clock_refresh_console_in_use(void)
{
	/* Set console in use expire time. */
	console_expire_time = get_time();
	console_expire_time.val += console_in_use_timeout_sec * SECOND;
	return;
}

void clock_uart2gpio(void)
{
	/* Is pimux to UART? */
	if (IS_BIT_SET(NPCX_DEVALT(0x0A), NPCX_DEVALTA_UART_SL)) {
		/* Change pinmux to GPIO and disable UART IRQ */
		task_disable_irq(NPCX_IRQ_UART);
		CLEAR_BIT(NPCX_DEVALT(0x0A), NPCX_DEVALTA_UART_SL);

		/*Enable MIWU for GPIO (UARTRX) */
		SET_BIT(NPCX_WKEN(1, 1), 0);
		/* Clear Pending bit of GPIO (UARTRX) */
		if (IS_BIT_SET(NPCX_WKPND(1, 1), 0))
			SET_BIT(NPCX_WKPCL(1, 1), 0);
		/* Disable MIWU IRQ */
		task_disable_irq(NPCX_IRQ_WKINTB_1);
	}
}

void clock_gpio2uart(void)
{
	/* Is Pending bit of GPIO (UARTRX) */
	if (IS_BIT_SET(NPCX_WKPND(1, 1), 0)) {
		/* Clear Pending bit of GPIO (UARTRX) */
		SET_BIT(NPCX_WKPCL(1, 1), 0);
		/* Refresh console in-use timer */
		clock_refresh_console_in_use();
		/* Disable MIWU & IRQ for GPIO (UARTRX) */
		CLEAR_BIT(NPCX_WKEN(1, 1), 0);
		/* Enable MIWU IRQ */
		task_enable_irq(NPCX_IRQ_WKINTB_1);
		/* Go back CR_SIN*/
		SET_BIT(NPCX_DEVALT(0x0A), NPCX_DEVALTA_UART_SL);
		/* Enable uart again */
		task_enable_irq(NPCX_IRQ_UART);
	}
}

/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
#ifdef SUPPORT_JTAG
	while (1) {
		/*
		 * TODO:(ML) JTAG bug: if debugger is connected,
		 * CPU can't enter wfi. Rev.B will fix it.
		 */
		;
	};
#else

	timestamp_t t0, t1;
	uint32_t next_evt_us;

	/*
	 * Initialize console in use to true and specify the console expire
	 * time in order to give a fixed window on boot in which the low speed
	 * clock will not be used in idle.
	 */
	console_expire_time.val = get_time().val + CONSOLE_IN_USE_ON_BOOT_TIME;

	while (1) {
		/*
		 * Disable interrupts before going to deep sleep in order to
		 * calculate the appropriate time to wake up. Note: the wfi
		 * instruction waits until an interrupt is pending, so it
		 * will still wake up even with interrupts disabled.
		 */
		interrupt_disable();

		/* Compute event delay */
		t0 = get_time();
		next_evt_us = __hw_clock_event_get() - t0.le.lo;

		/* Do we have enough time before next event to deep sleep. */
		if (DEEP_SLEEP_ALLOWED && (next_evt_us > WAKE_INTERVAL)
				/* Make sure it's over  console expired time */
				&& (t0.val > console_expire_time.val)) {
#if DEBUG_CLK
			/* Use GPIO to indicate SLEEP mode */
			CLEAR_BIT(NPCX_PDOUT(0), 0);
#endif
			idle_dsleep_cnt++;
			/* Set instant wake up mode */
			SET_BIT(NPCX_ENIDL_CTL, NPCX_ENIDL_CTL_LP_WK_CTL);

			/* Set deep idle - instant wake-up mode */
			NPCX_PMCSR = IDLE_PARAMS;
			/* UART-rx(console) become to GPIO (NONE INT mode) */
			clock_uart2gpio();
			/* Enter deep idle */
			asm("wfi");
			/* GPIO back to UART-rx (console) */
			clock_gpio2uart();

			/* Get time delay cause of deep idle */
			next_evt_us = __hw_clock_get_sleep_time();
			/* Fast forward timer according to wake-up timer. */
			t1.val = t0.val + next_evt_us;
			/* Record time spent in deep sleep. */
			idle_dsleep_time_us += next_evt_us;
			force_time(t1);
		} else {
#if DEBUG_CLK
			/* Use GPIO to indicate NORMAL mode */
			SET_BIT(NPCX_PDOUT(0), 0);
#endif
			idle_sleep_cnt++;
			/* normal idle : wait for interrupt */
			asm("wfi");
		}

		/*
		 * Restore interrupt
		 * RTOS will leave idle task to handle ISR which wakes up EC
		 */
		interrupt_enable();
	}
#endif
}
#endif /* CONFIG_LOW_POWER_IDLE */


#ifdef CONFIG_LOW_POWER_IDLE
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);
	ccprintf("Time spent in deep-sleep:            %.6lds\n",
			idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6lds\n", ts.val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats,
		"",
		"Print last idle stats",
		NULL);

/**
 * Configure deep sleep clock settings.
 */
static int command_dsleep(int argc, char **argv)
{
	int v;

	if (argc > 1) {
		if (parse_bool(argv[1], &v)) {
			/*
			 * Force deep sleep not to use low speed clock or
			 * allow it to use the low speed clock.
			 */
			if (v)
				disable_sleep(SLEEP_MASK_FORCE_NO_LOW_SPEED);
			else
				enable_sleep(SLEEP_MASK_FORCE_NO_LOW_SPEED);
		} else {
			/* Set console in use timeout. */
			char *e;
			v = strtoi(argv[1], &e, 10);
			if (*e)
				return EC_ERROR_PARAM1;

			console_in_use_timeout_sec = v;

			/* Refresh console in use to use new timeout. */
			clock_refresh_console_in_use();
		}
	}

	ccprintf("Sleep mask: %08x\n", sleep_mask);
	ccprintf("Console in use timeout:   %d sec\n",
			console_in_use_timeout_sec);
	ccprintf("PMCSR register:      0x%02x\n", NPCX_PMCSR);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dsleep, command_dsleep,
		"[ on | off | <timeout> sec]",
		"Deep sleep clock settings:\nUse 'on' to force deep "
		"sleep not to use low speed clock.\nUse 'off' to "
		"allow deep sleep to auto-select using the low speed "
		"clock.\n"
		"Give a timeout value for the console in use timeout.\n"
		"See also 'sleepmask'.",
		NULL);
#endif /* CONFIG_LOW_POWER_IDLE */
