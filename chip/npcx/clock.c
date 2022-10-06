/* Copyright 2014 The ChromiumOS Authors
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
#include "gpio_chip.h"
#include "hooks.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "uartn.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

#define WAKE_INTERVAL 61 /* Unit: 61 usec */
#define IDLE_PARAMS 0x7 /* Support deep idle, instant wake-up */

/* Low power idle statistics */
#ifdef CONFIG_LOW_POWER_IDLE
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
/*
 * Fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the low speed clock is not used.
 */
#define CONSOLE_IN_USE_ON_BOOT_TIME (15 * SECOND)
static int console_in_use_timeout_sec = 15;
static timestamp_t console_expire_time;
#endif

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
#if defined(CONFIG_CLOCK_SRC_EXTERNAL) && defined(NPCX_EXT32K_OSC_SUPPORT)
	/* Select external 32kHz crystal oscillator as LFCLK source. */
	SET_BIT(NPCX_LFCGCTL2, NPCX_LFCGCTL2_XT_OSC_SL_EN);
#endif

	/*
	 * Resting the OSC_CLK (even to the same value) will make the clock
	 * unstable for a little which can affect peripheral communication like
	 * eSPI. Skip this if not needed (e.g. RW jump)
	 */
	if (NPCX_HFCGN != HFCGN || NPCX_HFCGML != HFCGML ||
	    NPCX_HFCGMH != HFCGMH) {
		/*
		 * Configure frequency multiplier M/N values according to
		 * the requested OSC_CLK (Unit:Hz).
		 */
		NPCX_HFCGN = HFCGN;
		NPCX_HFCGML = HFCGML;
		NPCX_HFCGMH = HFCGMH;

		/* Load M and N values into the frequency multiplier */
		SET_BIT(NPCX_HFCGCTRL, NPCX_HFCGCTRL_LOAD);
		/* Wait for stable */
		while (IS_BIT_SET(NPCX_HFCGCTRL, NPCX_HFCGCTRL_CLK_CHNG))
			;
	}

	/* Set all clock prescalers of core and peripherals. */
#if defined(CHIP_FAMILY_NPCX5)
	NPCX_HFCGP = (FPRED << 4);
	NPCX_HFCBCD = (NPCX_HFCBCD & 0xF0) | (APB1DIV | (APB2DIV << 2));
#elif NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	NPCX_HFCGP = ((FPRED << 4) | AHB6DIV);
	NPCX_HFCBCD = (FIUDIV << 4);
	NPCX_HFCBCD1 = (APB1DIV | (APB2DIV << 4));
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	NPCX_HFCBCD2 = (APB3DIV | (APB4DIV << 4));
#else
	NPCX_HFCBCD2 = APB3DIV;
#endif
#endif

	/* Notify modules of frequency change */
	hook_notify(HOOK_FREQ_CHANGE);

	/* Configure alt. clock GPIOs (eg. optional 32KHz clock) */
	gpio_config_module(MODULE_CLOCK, 1);
}

#if defined(CHIP_FAMILY_NPCX5)
void clock_turbo(void)
{
	/* Configure Frequency multiplier values to 50MHz */
	NPCX_HFCGN = 0x02;
	NPCX_HFCGML = 0xEC;
	NPCX_HFCGMH = 0x0B;

	/* Load M and N values into the frequency multiplier */
	SET_BIT(NPCX_HFCGCTRL, NPCX_HFCGCTRL_LOAD);

	/* Wait for stable */
	while (IS_BIT_SET(NPCX_HFCGCTRL, NPCX_HFCGCTRL_CLK_CHNG))
		;

	/* Keep Core CLK & FMCLK are the same if Core CLK exceed 33MHz */
	NPCX_HFCGP = 0x00;

	/*
	 * Let APB2 equals Core CLK/2 if default APB2 clock is divisible
	 * by 1MHz
	 */
	NPCX_HFCBCD = NPCX_HFCBCD & 0xF3;
}
#elif NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
void clock_turbo(void)
{
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	/* For NPCX9:
	 * Increase CORE_CLK (CPU) as the same as OSC_CLK. Since
	 * CORE_CLK > 66MHz, we also need to set FIUDIV as 1 but
	 * can keep AHB6DIV to 0.
	 */
	NPCX_HFCGP = 0x00;
#else
	/* For NPCX7:
	 * Increase CORE_CLK (CPU) as the same as OSC_CLK. Since
	 * CORE_CLK > 66MHz, we also need to set AHB6DIV and FIUDIV as 1.
	 */
	NPCX_HFCGP = 0x01;
#endif
	NPCX_HFCBCD = BIT(4);
}

void clock_normal(void)
{
	/* Set CORE_CLK (CPU), AHB6_CLK and FIU_CLK back to original values. */
	NPCX_HFCGP = ((FPRED << 4) | AHB6DIV);
	NPCX_HFCBCD = (FIUDIV << 4);
}

void clock_enable_module(enum module_id module, int enable)
{
	/* Assume we have a single task using MODULE_FAST_CPU */
	if (module == MODULE_FAST_CPU) {
		if (enable)
			clock_turbo();
		else
			clock_normal();
	}
}

#endif

/**
 * Return the current clock frequency in Hz.
 */
int clock_get_freq(void)
{
	return CORE_CLK;
}

/**
 * Return the current FMUL clock frequency in Hz.
 */
int clock_get_fm_freq(void)
{
	return FMCLK;
}

/**
 * Return the current APB1 clock frequency in Hz.
 */
int clock_get_apb1_freq(void)
{
	return NPCX_APB_CLOCK(1);
}

/**
 * Return the current APB2 clock frequency in Hz.
 */
int clock_get_apb2_freq(void)
{
	return NPCX_APB_CLOCK(2);
}

/**
 * Return the current APB3 clock frequency in Hz.
 */
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
int clock_get_apb3_freq(void)
{
	return NPCX_APB_CLOCK(3);
}
#endif

/**
 * Wait for a number of clock cycles.
 *
 * Simple busy waiting for use before clocks/timers are initialized.
 *
 * @param cycles	Number of cycles to wait.
 */
void clock_wait_cycles(uint32_t cycles)
{
	asm volatile("1: subs %0, #1\n"
		     "   bne 1b\n"
		     : "+r"(cycles));
}

#ifdef CONFIG_LOW_POWER_IDLE
void clock_refresh_console_in_use(void)
{
	/* Set console in use expire time. */
	console_expire_time = get_time();
	console_expire_time.val += console_in_use_timeout_sec * SECOND;
	return;
}

#if defined(CHIP_FAMILY_NPCX5)
void clock_uart2gpio(void)
{
	/* Is pimux to UART? */
	if (npcx_is_uart()) {
		/* Flush tx before enter deep idle */
		uart_tx_flush();
		/* Change pinmux to GPIO and disable UART IRQ */
		task_disable_irq(NPCX_IRQ_UART);
		/* Set to GPIO */
		npcx_uart2gpio();
		/* Clear pending wakeup */
		uart_clear_pending_wakeup();
		/* Enable MIWU for GPIO (UARTRX) */
		uart_enable_wakeup(1);
	}
}

void clock_gpio2uart(void)
{
	/* Is Pending bit of GPIO (UARTRX) */
	if (uart_is_wakeup_from_gpio()) {
		/* Refresh console in-use timer */
		clock_refresh_console_in_use();
		/* Disable MIWU for GPIO (UARTRX) */
		uart_enable_wakeup(0);
		/* Go back CR_SIN */
		npcx_gpio2uart();
		/* Enable uart again */
		task_enable_irq(NPCX_IRQ_UART);
	}
}
#endif

/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	timestamp_t t0, t1;
	uint32_t next_evt;
	uint32_t next_evt_us;
	uint16_t evt_count;

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
		next_evt = __hw_clock_event_get();

		/* Do we have enough time before next event to deep sleep. */
		if (DEEP_SLEEP_ALLOWED &&
		    /*
		     * Our HW timer doesn't tick in deep sleep - we do manual
		     * adjustment based on sleep duration after wake. Avoid
		     * the tricky overflow case by waiting out the period just
		     * before overflow.
		     */
		    next_evt != EVT_MAX_EXPIRED_US &&
		    /* Ensure event hasn't already expired */
		    next_evt > t0.le.lo &&
		    /* Ensure we have sufficient time before expiration */
		    next_evt - t0.le.lo > WAKE_INTERVAL &&
		    /* Make sure it's over console expired time */
		    t0.val > console_expire_time.val) {
#if DEBUG_CLK
			/* Use GPIO to indicate SLEEP mode */
			CLEAR_BIT(NPCX_PDOUT(0), 0);
#endif
			idle_dsleep_cnt++;

			/* Enable Host access wakeup */
			SET_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 6);

#if defined(CHIP_FAMILY_NPCX5)
			/* UART-rx(console) become to GPIO (NONE INT mode) */
			clock_uart2gpio();
#elif NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
			uartn_wui_en(CONFIG_CONSOLE_UART);
#endif

			/*
			 * Disable input buffer of all 1.8v i2c ports before
			 * entering deep sleep for better power consumption.
			 */
			gpio_enable_1p8v_i2c_wake_up_input(0);

			/* Set deep idle - instant wake-up mode */
			NPCX_PMCSR = IDLE_PARAMS;

			/* Get current counter value of event timer */
			evt_count = __hw_clock_event_count();

			/*
			 * TODO (ML): We found the same symptom of idle occurs
			 * after wake-up from deep idle. Please see task.c for
			 * more detail.
			 * Workaround: Apply the same bypass of idle.
			 */
			asm("push {r0-r5}\n"
			    "wfi\n"
			    "ldm %0, {r0-r5}\n"
			    "pop {r0-r5}\n"
			    "isb\n" ::"r"(0x100A8000));

			/* Get time delay cause of deep idle */
			next_evt_us = __hw_clock_get_sleep_time(evt_count);

			/*
			 * Clear PMCSR manually in case there's wake-up between
			 * setting it and wfi.
			 */
			NPCX_PMCSR = 0;
#if defined(CHIP_FAMILY_NPCX5)
			/* GPIO back to UART-rx (console) */
			clock_gpio2uart();
#endif

			/* Enable input buffer of all 1.8v i2c ports. */
			gpio_enable_1p8v_i2c_wake_up_input(1);

			/* Record time spent in deep sleep. */
			idle_dsleep_time_us += next_evt_us;

			/* Fast forward timer according to wake-up timer. */
			t1.val = t0.val + next_evt_us;
			/* Leave overflow situation for ITIM32 */
			if (t1.le.hi == t0.le.hi)
				force_time(t1);
		} else {
#if DEBUG_CLK
			/* Use GPIO to indicate NORMAL mode */
			SET_BIT(NPCX_PDOUT(0), 0);
#endif
			idle_sleep_cnt++;

			/*
			 * Using host access to make sure M4 core clock will
			 * return when the eSPI accesses the Host modules if
			 * CSAE bit is set. Please notice this symptom only
			 * occurs at npcx5.
			 */
#if defined(CHIP_FAMILY_NPCX5) && defined(CONFIG_HOST_INTERFACE_ESPI)
			/* Enable Host access wakeup */
			SET_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 6);
#endif
			/*
			 * Normal idle : wait for interrupt
			 * TODO (ML): Workaround method for wfi issue.
			 * Please see task.c for more detail
			 */
			asm("push {r0-r5}\n"
			    "wfi\n"
			    "ldm %0, {r0-r5}\n"
			    "pop {r0-r5}\n"
			    "isb\n" ::"r"(0x100A8000));
		}

		/*
		 * Restore interrupt
		 * RTOS will leave idle task to handle ISR which wakes up EC
		 */
		interrupt_enable();
	}
}
#endif /* CONFIG_LOW_POWER_IDLE */

#ifdef CONFIG_LOW_POWER_IDLE
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, const char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);
	ccprintf("Time spent in deep-sleep:            %.6llds\n",
		 idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6llds\n", ts.val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");

/**
 * Configure deep sleep clock settings.
 */
static int command_dsleep(int argc, const char **argv)
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

	ccprintf("Sleep mask: %08x\n", (int)sleep_mask);
	ccprintf("Console in use timeout:   %d sec\n",
		 console_in_use_timeout_sec);
	ccprintf("PMCSR register:      0x%02x\n", NPCX_PMCSR);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dsleep, command_dsleep, "[ on | off | <timeout> sec]",
			"Deep sleep clock settings:\nUse 'on' to force deep "
			"sleep not to use low speed clock.\nUse 'off' to "
			"allow deep sleep to auto-select using the low speed "
			"clock.\n"
			"Give a timeout value for the console in use timeout.\n"
			"See also 'sleepmask'.");
#endif /* CONFIG_LOW_POWER_IDLE */
