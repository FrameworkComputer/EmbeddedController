/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
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

#define PLL_CLOCK 66666667  /* System clock = 200MHz PLL/3 = 66.667MHz */

#ifdef CONFIG_LOW_POWER_USE_LFIOSC
/*
 * Length of time for the processor to wake up from deep sleep. Actual
 * measurement gives anywhere up to 780us, depending on the mode it is coming
 * out of. The datasheet gives a maximum of 846us, for coming out of deep
 * sleep in our worst case deep sleep mode.
 */
#define DEEP_SLEEP_RECOVER_TIME_USEC 850
#else
/*
 * Length of time for the processor to wake up from deep sleep. Datasheet
 * maximum is 145us, but in practice have seen as much as 336us.
 */
#define DEEP_SLEEP_RECOVER_TIME_USEC 400
#endif

/* Low power idle statistics */
#ifdef CONFIG_LOW_POWER_IDLE
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
static int dsleep_recovery_margin_us = 1000000;

/*
 * Fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the low speed clock is not used.
 */
#define CONSOLE_IN_USE_ON_BOOT_TIME (15*SECOND)

static int console_in_use_timeout_sec = 60;
static timestamp_t console_expire_time;
#endif

static int freq;

/**
 * Disable the PLL; run off internal oscillator.
 */
static void disable_pll(void)
{
	/* Switch to 16MHz internal oscillator and power down the PLL */
	LM4_SYSTEM_RCC = LM4_SYSTEM_RCC_SYSDIV(0) |
		LM4_SYSTEM_RCC_BYPASS |
		LM4_SYSTEM_RCC_PWRDN |
		LM4_SYSTEM_RCC_OSCSRC(1) |
		LM4_SYSTEM_RCC_MOSCDIS;

#ifdef CONFIG_LOW_POWER_IDLE
	/*
	 * If using the low power idle, then set the ACG bit, which specifies
	 * that the sleep and deep sleep modes are using their own clock gating
	 * registers SCGC and DCGS respectively instead of using the run mode
	 * clock gating registers RCGC.
	 */
	LM4_SYSTEM_RCC |= LM4_SYSTEM_RCC_ACG;
#endif

	LM4_SYSTEM_RCC2 &= ~LM4_SYSTEM_RCC2_USERCC2;

	freq = INTERNAL_CLOCK;
}

/**
 * Enable the PLL to run at full clock speed.
 */
static void enable_pll(void)
{
	/* Disable the PLL so we can reconfigure it */
	disable_pll();

	/*
	 * Enable the PLL (PWRDN is no longer set) and set divider.  PLL is
	 * still bypassed, since it hasn't locked yet.
	 */
	LM4_SYSTEM_RCC = LM4_SYSTEM_RCC_SYSDIV(2) |
		LM4_SYSTEM_RCC_USESYSDIV |
		LM4_SYSTEM_RCC_BYPASS |
		LM4_SYSTEM_RCC_OSCSRC(1) |
		LM4_SYSTEM_RCC_MOSCDIS;

#ifdef CONFIG_LOW_POWER_IDLE
	/*
	 * If using the low power idle, then set the ACG bit, which specifies
	 * that the sleep and deep sleep modes are using their own clock gating
	 * registers SCGC and DCGS respectively instead of using the run mode
	 * clock gating registers RCGC.
	 */
	LM4_SYSTEM_RCC |= LM4_SYSTEM_RCC_ACG;
#endif

	/* Wait for the PLL to lock */
	clock_wait_cycles(1024);
	while (!(LM4_SYSTEM_PLLSTAT & 1))
		;

	/* Remove bypass on PLL */
	LM4_SYSTEM_RCC &= ~LM4_SYSTEM_RCC_BYPASS;
	freq = PLL_CLOCK;
}

void clock_enable_pll(int enable, int notify)
{
	if (enable)
		enable_pll();
	else
		disable_pll();

	/* Notify modules of frequency change */
	if (notify)
		hook_notify(HOOK_FREQ_CHANGE);
}

void clock_wait_cycles(uint32_t cycles)
{
	asm volatile("1: subs %0, #1\n"
		     "   bne 1b\n" : "+r"(cycles));
}

int clock_get_freq(void)
{
	return freq;
}

void clock_init(void)
{
#ifdef BOARD_BDS
	/*
	 * Perform an auto calibration of the internal oscillator using the
	 * 32.768KHz hibernate clock, unless we've already done so.  This is
	 * only necessary on A2 silicon as on BDS; A3 silicon is all
	 * factory-trimmed.
	 */
	if ((LM4_SYSTEM_PIOSCSTAT & 0x300) != 0x100) {
		/* Start calibration */
		LM4_SYSTEM_PIOSCCAL = 0x80000000;
		LM4_SYSTEM_PIOSCCAL = 0x80000200;
		/* Wait for result */
		clock_wait_cycles(16);
		while (!(LM4_SYSTEM_PIOSCSTAT & 0x300))
			;
	}
#else
	/*
	 * Only BDS has an external crystal; other boards don't have one, and
	 * can disable main oscillator control to reduce power consumption.
	 */
	LM4_SYSTEM_MOSCCTL = 0x04;
#endif

	/* Make sure PLL is disabled */
	disable_pll();
}

void clock_enable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	if (mode & CGC_MODE_RUN)
		*(LM4_SYSTEM_RCGC_BASE + offset) |= mask;

	if (mode & CGC_MODE_SLEEP)
		*(LM4_SYSTEM_SCGC_BASE + offset) |= mask;

	if (mode & CGC_MODE_DSLEEP)
		*(LM4_SYSTEM_DCGC_BASE + offset) |= mask;

	/* Wait for clock change to take affect. */
	clock_wait_cycles(3);
}

void clock_disable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	if (mode & CGC_MODE_RUN)
		*(LM4_SYSTEM_RCGC_BASE + offset) &= ~mask;

	if (mode & CGC_MODE_SLEEP)
		*(LM4_SYSTEM_SCGC_BASE + offset) &= ~mask;

	if (mode & CGC_MODE_DSLEEP)
		*(LM4_SYSTEM_DCGC_BASE + offset) &= ~mask;
}

/*
 * The low power idle task does not support using the EEPROM,
 * because it is dangerous to go to deep sleep while EEPROM
 * transaction is in progress. To fix, LM4_EEPROM_EEDONE, should
 * be checked before going in to deep sleep.
 */
#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_EEPROM)
#error "Low power idle mode does not support use of EEPROM"
#endif

#ifdef CONFIG_LOW_POWER_IDLE

void clock_refresh_console_in_use(void)
{
	disable_sleep(SLEEP_MASK_CONSOLE);

	/* Set console in use expire time. */
	console_expire_time = get_time();
	console_expire_time.val += console_in_use_timeout_sec * SECOND;

}

/* Low power idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	timestamp_t t0, t1, rtc_t0, rtc_t1;
	int next_delay = 0;
	int time_for_dsleep, margin_us;
	int use_low_speed_clock;

	/* Enable the hibernate IRQ used to wake up from deep sleep */
	system_enable_hib_interrupt();

	/* Set SRAM and flash power management to 'low power' in deep sleep. */
	LM4_SYSTEM_DSLPPWRCFG = 0x23;

	/* Enable JTAG interrupt which will notify us when JTAG is in use. */
	gpio_enable_interrupt(GPIO_JTAG_TCK);

	/*
	 * Initialize console in use to true and specify the console expire
	 * time in order to give a fixed window on boot in which the low speed
	 * clock will not be used in idle.
	 */
	disable_sleep(SLEEP_MASK_CONSOLE);
	console_expire_time.val = get_time().val + CONSOLE_IN_USE_ON_BOOT_TIME;

	/*
	 * Print when the idle task starts.  This is the lowest priority task,
	 * so this only starts once all other tasks have gotten a chance to do
	 * their task inits and have gone to sleep.
	 */
	CPRINTS("low power idle task started");

	while (1) {
		/*
		 * Disable interrupts before going to deep sleep in order to
		 * calculate the appropriate time to wake up. Note: the wfi
		 * instruction waits until an interrupt is pending, so it
		 * will still wake up even with interrupts disabled.
		 */
		interrupt_disable();

		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		/* Do we have enough time before next event to deep sleep. */
		time_for_dsleep = next_delay > (DEEP_SLEEP_RECOVER_TIME_USEC +
						HIB_SET_RTC_MATCH_DELAY_USEC);

		if (DEEP_SLEEP_ALLOWED && time_for_dsleep) {
			/* Deep-sleep in STOP mode. */
			idle_dsleep_cnt++;

			/* Check if the console use has expired. */
			if ((sleep_mask & SLEEP_MASK_CONSOLE) &&
					t0.val > console_expire_time.val) {
				/* Enable low speed deep sleep. */
				enable_sleep(SLEEP_MASK_CONSOLE);

				/*
				 * Wait one clock before checking if low speed
				 * deep sleep is allowed to give time for
				 * sleep mask to update.
				 */
				clock_wait_cycles(1);

				if (LOW_SPEED_DEEP_SLEEP_ALLOWED)
					CPRINTS("Disabling console in "
						"deep sleep");
			}

			/*
			 * Determine if we should use a lower clock speed or
			 * keep the same (16MHz) clock in deep sleep. Use the
			 * lower speed only if the sleep mask specifies that low
			 * speed sleep is allowed, the console UART TX is not
			 * busy, and the console UART buffer is empty.
			 */
			use_low_speed_clock = LOW_SPEED_DEEP_SLEEP_ALLOWED &&
				!uart_tx_in_progress() && uart_buffer_empty();

#ifdef CONFIG_LOW_POWER_USE_LFIOSC
			/* Set the deep sleep clock register. Use either the
			 *  normal PIOSC (16MHz) or the LFIOSC (32kHz). */
			LM4_SYSTEM_DSLPCLKCFG = use_low_speed_clock ?
					0x32 : 0x10;
#else
			/*
			 * Set the deep sleep clock register. Use either the
			 * PIOSC with no divider (16MHz) or the PIOSC with
			 * a /64 divider (250kHz).
			 */
			LM4_SYSTEM_DSLPCLKCFG = use_low_speed_clock ?
					0x1f800010 : 0x10;
#endif

			/*
			 * If using low speed clock, disable console.
			 * This will also convert the console RX pin to a GPIO
			 * and set an edge interrupt to wake us from deep sleep
			 * if any action occurs on console.
			 */
			if (use_low_speed_clock)
				uart_enter_dsleep();

			/* Set deep sleep bit. */
			CPU_SCB_SYSCTRL |= 0x4;

			/* Record real time before sleeping. */
			rtc_t0 = system_get_rtc();

			/*
			 * Set RTC interrupt in time to wake up before
			 * next event.
			 */
			system_set_rtc_alarm(0, next_delay -
						DEEP_SLEEP_RECOVER_TIME_USEC);

			/* Wait for interrupt: goes into deep sleep. */
			asm("wfi");

			/* Clear deep sleep bit. */
			CPU_SCB_SYSCTRL &= ~0x4;

			/* Disable and clear RTC interrupt. */
			system_reset_rtc_alarm();

			/* Fast forward timer according to RTC counter. */
			rtc_t1 = system_get_rtc();
			t1.val = t0.val + (rtc_t1.val - rtc_t0.val);
			force_time(t1);

			/* If using low speed clock, re-enable the console. */
			if (use_low_speed_clock)
				uart_exit_dsleep();

			/* Record time spent in deep sleep. */
			idle_dsleep_time_us += (rtc_t1.val - rtc_t0.val);

			/* Calculate how close we were to missing deadline */
			margin_us = next_delay - (int)(rtc_t1.val - rtc_t0.val);
			if (margin_us < 0)
				CPRINTS("overslept by %dus", -margin_us);

			/* Record the closest to missing a deadline. */
			if (margin_us < dsleep_recovery_margin_us)
				dsleep_recovery_margin_us = margin_us;
		} else {
			idle_sleep_cnt++;

			/* Normal idle : only CPU clock stopped. */
			asm("wfi");
		}
		interrupt_enable();
	}
}
#endif /* CONFIG_LOW_POWER_IDLE */

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_SLEEP
/**
 * Measure baseline for power consumption.
 *
 * Levels :
 *   0 : CPU running in tight loop
 *   1 : CPU running in tight loop but peripherals gated
 *   2 : CPU in sleep mode
 *   3 : CPU in sleep mode and peripherals gated
 *   4 : CPU in deep sleep mode
 *   5 : CPU in deep sleep mode and peripherals gated
 *
 * Clocks :
 *   0 : No change
 *   1 : 16MHz
 *   2 : 1 MHz
 *   3 : 30kHz
 *
 * SRAM Power Management:
 *   0 : Active
 *   1 : Standby
 *   3 : Low Power
 *
 * Flash Power Management:
 *   0 : Active
 *   2 : Low Power
 */
static int command_sleep(int argc, char **argv)
{
	int level = 0;
	int clock = 0;
	int sram_pm = 0;
	int flash_pm = 0;
	uint32_t uartibrd = 0;
	uint32_t uartfbrd = 0;

	if (argc >= 2)
		level = strtoi(argv[1], NULL, 10);
	if (argc >= 3)
		clock = strtoi(argv[2], NULL, 10);
	if (argc >= 4)
		sram_pm = strtoi(argv[3], NULL, 10);
	if (argc >= 5)
		flash_pm = strtoi(argv[4], NULL, 10);

#ifdef BOARD_BDS
	/* Remove LED current sink. */
	gpio_set_level(GPIO_DEBUG_LED, 0);
#endif

	ccprintf("Sleep : level %d, clock %d, sram pm %d, flash_pm %d...\n",
			level, clock, sram_pm, flash_pm);
	cflush();

	/* Set clock speed. */
	if (clock) {
		/* Use ROM code function to set the clock */
		void **func_table = (void **)*(uint32_t *)0x01000044;
		void (*rom_clock_set)(uint32_t rcc) = func_table[23];

		/* Disable interrupts. */
		asm volatile("cpsid i");

		switch (clock) {
		case 1: /* 16MHz IOSC */
			uartibrd = 17;
			uartfbrd = 23;
			rom_clock_set(0x00000d51);
			break;
		case 2: /* 1MHz IOSC */
			uartibrd = 1;
			uartfbrd = 5;
			rom_clock_set(0x07C00d51);
			break;
		case 3: /* 30 kHz */
			uartibrd = 0;
			uartfbrd = 0;
			rom_clock_set(0x00000d71);
			break;
		}

		/*
		 * TODO(crosbug.com/p/23795): move this to the UART module;
		 * ugly to have UARTisms here.  Also note this only fixes
		 * UART0, not UART1.  Should just be able to trigger
		 * HOOK_FREQ_CHANGE and have that take care of it.
		 */
		if (uartfbrd) {
			/* Disable the port via UARTCTL and add HSE. */
			LM4_UART_CTL(0) = 0x0320;
			/* Set the baud rate divisor. */
			LM4_UART_IBRD(0) = uartibrd;
			LM4_UART_FBRD(0) = uartfbrd;
			/* Poke UARTLCRH to make the new divisor take effect. */
			LM4_UART_LCRH(0) = LM4_UART_LCRH(0);
			/* Enable the port. */
			LM4_UART_CTL(0) |= 0x0001;
		}
		asm volatile("cpsie i");
	}

	if (uartfbrd) {
		ccprintf("We are still alive. RCC=%08x\n", LM4_SYSTEM_RCC);
		cflush();
	}

	/* Enable interrupts. */
	asm volatile("cpsid i");

	/* gate peripheral clocks */
	if (level & 1) {
		clock_disable_peripheral(CGC_OFFSET_WD,     0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_TIMER,  0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_GPIO,   0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_DMA,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_HIB,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_UART,   0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_SSI,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_I2C,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_ADC,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_LPC,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_PECI,   0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_FAN,    0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_EEPROM, 0xffffffff,
				CGC_MODE_ALL);
		clock_disable_peripheral(CGC_OFFSET_WTIMER, 0xffffffff,
				CGC_MODE_ALL);
	}

	/* Set deep sleep bit. */
	if (level >= 4)
		CPU_SCB_SYSCTRL |= 0x4;

	/* Set SRAM and flash PM for sleep and deep sleep. */
	LM4_SYSTEM_SLPPWRCFG = (flash_pm << 4) | sram_pm;
	LM4_SYSTEM_DSLPPWRCFG = (flash_pm << 4) | sram_pm;

	/* Go to low power mode (forever ...) */
	if (level > 1)
		while (1) {
			asm("wfi");
			watchdog_reload();
		}
	else
		while (1)
			watchdog_reload();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sleep, command_sleep,
			"[level [clock] [sram pm] [flash pm]]",
			"Drop into sleep");
#endif /* CONFIG_CMD_SLEEP */

#ifdef CONFIG_CMD_PLL

static int command_pll(int argc, char **argv)
{
	int v;

	/* Toggle the PLL */
	if (argc > 1) {
		if (parse_bool(argv[1], &v)) {
			clock_enable_pll(v, 1);
		} else {
			/* Disable PLL and set extra divider */
			char *e;
			v = strtoi(argv[1], &e, 10);
			if (*e)
				return EC_ERROR_PARAM1;

			LM4_SYSTEM_RCC = LM4_SYSTEM_RCC_SYSDIV(v - 1) |
				LM4_SYSTEM_RCC_BYPASS |
				LM4_SYSTEM_RCC_PWRDN |
				LM4_SYSTEM_RCC_OSCSRC(1) |
				LM4_SYSTEM_RCC_MOSCDIS;

			freq = INTERNAL_CLOCK / v;

			/* Notify modules of frequency change */
			hook_notify(HOOK_FREQ_CHANGE);
		}
	}

	/* Print current PLL state */
	ccprintf("RCC:     0x%08x\n", LM4_SYSTEM_RCC);
	ccprintf("RCC2:    0x%08x\n", LM4_SYSTEM_RCC2);
	ccprintf("PLLSTAT: 0x%08x\n", LM4_SYSTEM_PLLSTAT);
	ccprintf("Clock:   %d Hz\n", clock_get_freq());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pll, command_pll,
			"[ on | off | <div> ]",
			"Get/set PLL state");

#endif /* CONFIG_CMD_PLL */

#ifdef CONFIG_CMD_CLOCKGATES
/**
 * Print all clock gating registers
 */
static int command_clock_gating(int argc, char **argv)
{
	ccprintf("         Run       , Sleep     , Deep Sleep\n");

	ccprintf("WD:      0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_WD));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_WD));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_WD));

	ccprintf("TIMER:   0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_TIMER));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_TIMER));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_TIMER));

	ccprintf("GPIO:    0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_GPIO));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_GPIO));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_GPIO));

	ccprintf("DMA:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_DMA));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_DMA));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_DMA));

	ccprintf("HIB:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_HIB));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_HIB));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_HIB));

	ccprintf("UART:    0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_UART));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_UART));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_UART));

	ccprintf("SSI:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_SSI));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_SSI));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_SSI));

	ccprintf("I2C:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_I2C));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_I2C));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_I2C));

	ccprintf("ADC:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_ADC));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_ADC));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_ADC));

	ccprintf("LPC:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_LPC));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_LPC));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_LPC));

	ccprintf("PECI:    0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_PECI));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_PECI));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_PECI));

	ccprintf("FAN:     0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_FAN));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_FAN));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_FAN));

	ccprintf("EEPROM:  0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_EEPROM));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_EEPROM));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_EEPROM));

	ccprintf("WTIMER:  0x%08x, ",
				*(LM4_SYSTEM_RCGC_BASE + CGC_OFFSET_WTIMER));
	ccprintf("0x%08x, ",	*(LM4_SYSTEM_SCGC_BASE + CGC_OFFSET_WTIMER));
	ccprintf("0x%08x\n",	*(LM4_SYSTEM_DCGC_BASE + CGC_OFFSET_WTIMER));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clockgates, command_clock_gating,
			"",
			"Get state of the clock gating controls regs");
#endif /* CONFIG_CMD_CLOCKGATES */

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
	ccprintf("Deep-sleep closest to wake deadline: %dus\n",
			dsleep_recovery_margin_us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats,
			"",
			"Print last idle stats");

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
	ccprintf("DSLPCLKCFG register:      0x%08x\n", LM4_SYSTEM_DSLPCLKCFG);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dsleep, command_dsleep,
			"[ on | off | <timeout> sec]",
			"Deep sleep clock settings:\nUse 'on' to force deep "
			"sleep not to use low speed clock.\nUse 'off' to "
			"allow deep sleep to auto-select using the low speed "
			"clock.\n"
			"Give a timeout value for the console in use timeout.\n"
			"See also 'sleepmask'.");
#endif /* CONFIG_LOW_POWER_IDLE */

