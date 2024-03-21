/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "vboot_hash.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

#ifdef CONFIG_LOW_POWER_IDLE

#define HTIMER_DIV_1_US_MAX (1998848)
#define HTIMER_DIV_1_1SEC (0x8012)

/* Recovery time for HvySlp2 is 0 us */
#define HEAVY_SLEEP_RECOVER_TIME_USEC 75

#define SET_HTIMER_DELAY_USEC 200

static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t total_idle_dsleep_time_us;

#ifdef CONFIG_MCHP_DEEP_SLP_DEBUG
static uint32_t pcr_slp_en[MCHP_PCR_SLP_RST_REG_MAX];
static uint32_t pcr_clk_req[MCHP_PCR_SLP_RST_REG_MAX];
static uint32_t ecia_result[MCHP_INT_GIRQ_NUM];
#endif

/*
 * Fixed amount of time to keep the console in use flag true after
 * boot in order to give a permanent window in which the heavy sleep
 * mode is not used.
 */
static int console_in_use_timeout_sec = 60;
static timestamp_t console_expire_time;
#endif /*CONFIG_LOW_POWER_IDLE */

static int freq = 48000000;

void clock_wait_cycles(uint32_t cycles)
{
	asm volatile("1: subs %0, #1\n"
		     "   bne 1b\n"
		     : "+r"(cycles));
}

int clock_get_freq(void)
{
	return freq;
}

/*
 * MEC170x and MEC152x have the same 32 KHz clock enable hardware.
 * MEC172x 32 KHz clock configuration is different and includes
 * hardware to check the crystal before switching and to monitor
 * the 32 KHz input if desired.
 */
#ifdef CHIP_FAMILY_MEC172X
/* 32 KHz crystal connected in parallel */
static inline void config_32k_src_crystal(void)
{
	MCHP_VBAT_CSS = MCHP_VBAT_CSS_XTAL_EN | MCHP_VBAT_CSS_SRC_XTAL;
}

/* 32 KHz source is 32KHZ_IN pin which must be configured */
static inline void config_32k_src_se_input(void)
{
	MCHP_VBAT_CSS = MCHP_VBAT_CSS_SIL32K_EN | MCHP_VBAT_CSS_SRC_SWPS;
}

static inline void config_32k_src_sil_osc(void)
{
	MCHP_VBAT_CSS = MCHP_VBAT_CSS_SIL32K_EN;
}

#else
static void config_32k_src_crystal(void)
{
	MCHP_VBAT_CE = MCHP_VBAT_CE_XOSEL_PAR |
		       MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_CRYSTAL;
}

/* 32 KHz source is 32KHZ_IN pin which must be configured */
static inline void config_32k_src_se_input(void)
{
	MCHP_VBAT_CE = MCHP_VBAT_CE_32K_DOMAIN_32KHZ_IN_PIN |
		       MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_INT;
}

static inline void config_32k_src_sil_osc(void)
{
	MCHP_VBAT_CE = ~(MCHP_VBAT_CE_32K_DOMAIN_32KHZ_IN_PIN |
			 MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_CRYSTAL);
}
#endif

/** clock_init
 * @note
 * MCHP MEC implements 4 control bits in the VBAT Clock Enable register.
 * It also implements an internal silicon 32KHz +/- 2% oscillator powered
 * by VBAT.
 * b[3] = XOSEL 0=parallel, 1=single-ended
 * b[2] = 32KHZ_SOURCE specifies source of always-on clock domain
 *        0=internal silicon oscillator
 *        1=crystal XOSEL pin(s)
 * b[1] = EXT_32K use always-on clock domain or external 32KHZ_IN pin
 *        0=32K source is always-on clock domain
 *        1=32K source is 32KHZ_IN pin (GPIO 0165)
 * b[0] = 32K_SUPPRESS
 *        0=32K clock domain stays enabled if VTR is off. Powered by VBAT
 *        1=32K clock domain is disabled if VTR is off.
 * Set b[3] based on CONFIG_CLOCK_CRYSTAL
 * Set b[2:0] = 100b
 *    b[0]=0 32K clock domain always on (requires VBAT if VTR is off)
 *    b[1]=0 32K source is the 32K clock domain NOT the 32KHZ_IN pin
 *    b[2]=1 If activity detected on crystal pins switch 32K input from
 *           internal silicon oscillator to XOSEL pin(s) based on b[3].
 */
void clock_init(void)
{
	if (IS_ENABLED(CONFIG_CLOCK_SRC_EXTERNAL))
		if (IS_ENABLED(CONFIG_CLOCK_CRYSTAL))
			config_32k_src_crystal();
		else
			/* 32KHz 50% duty waveform on 32KHZ_IN pin */
			config_32k_src_se_input();
	else
		/* Use internal silicon 32KHz OSC */
		config_32k_src_sil_osc();

	/* Wait for PLL to lock onto 32KHz source (OSC_LOCK == 1) */
	while (!(MCHP_PCR_CHIP_OSC_ID & 0x100))
		;
}

/**
 * Speed through boot + vboot hash calculation, dropping our processor
 * clock only after vboot hashing is completed.
 */
static void clock_turbo_disable(void);
DECLARE_DEFERRED(clock_turbo_disable);

static void clock_turbo_disable(void)
{
#ifdef CONFIG_VBOOT_HASH
	if (vboot_hash_in_progress())
		hook_call_deferred(&clock_turbo_disable_data, 100 * MSEC);
	else
#endif
		/* Use 12 MHz processor clock for power savings */
		MCHP_PCR_PROC_CLK_CTL = MCHP_PCR_CLK_CTL_12MHZ;
}
DECLARE_HOOK(HOOK_INIT, clock_turbo_disable, HOOK_PRIO_INIT_VBOOT_HASH + 1);

/**
 * initialization of Hibernation timer 0
 * Clear PCR sleep enable.
 * GIRQ=21, aggregator bit = 1, Direct NVIC = 112
 * NVIC direct connect interrupts are used for all peripherals
 * (exception GPIO's) then the MCHP_INT_BLK_EN GIRQ bit should not be
 * set.
 */
void htimer_init(void)
{
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_HTMR0);
	MCHP_HTIMER_PRELOAD(0) = 0; /* disable at beginning */
	MCHP_INT_SOURCE(MCHP_HTIMER_GIRQ) = MCHP_HTIMER_GIRQ_BIT(0);
	MCHP_INT_ENABLE(MCHP_HTIMER_GIRQ) = MCHP_HTIMER_GIRQ_BIT(0);

	task_enable_irq(MCHP_IRQ_HTIMER0);
}

/**
 * Use hibernate module to set up an htimer interrupt at a given
 * time from now
 *
 * @param seconds      Number of seconds before htimer interrupt
 * @param microseconds Number of microseconds before htimer interrupt
 * @note hibernation timer input clock is 32.768KHz.
 * Control register bit[0] selects the divider.
 * 0 is divide by 1 for 30.5 us per LSB for a maximum of
 *	65535 * 30.5 us = 1998817.5 us or 32.786 counts per second
 * 1 is divide by 4096 for 0.125 s per LSB for a maximum of ~2 hours.
 *	65535 * 0.125 s ~ 8192 s = 2.27 hours
 */
void system_set_htimer_alarm(uint32_t seconds, uint32_t microseconds)
{
	uint32_t hcnt, ns;
	uint8_t hctrl;

	MCHP_HTIMER_PRELOAD(0) = 0; /* disable */

	if (microseconds > 1000000ul) {
		ns = (microseconds / 1000000ul);
		microseconds %= 1000000ul;
		if ((0xfffffffful - seconds) > ns)
			seconds += ns;
		else
			seconds = 0xfffffffful;
	}

	if (seconds > 1) {
		hcnt = (seconds << 3); /* divide by 0.125 */
		if (hcnt > 0xfffful)
			hcnt = 0xfffful;
		hctrl = 1;
	} else {
		/*
		 * approximate(~2% error) as seconds is 0 or 1
		 * seconds / 30.5e-6 + microseconds / 30.5
		 */
		hcnt = (seconds << 15) + (microseconds >> 5) +
		       (microseconds >> 10);
		hctrl = 0;
	}

	MCHP_HTIMER_CONTROL(0) = hctrl;
	MCHP_HTIMER_PRELOAD(0) = hcnt;
}

#ifdef CONFIG_LOW_POWER_IDLE

/**
 * return time slept in micro-seconds
 */
static timestamp_t system_get_htimer(void)
{
	uint16_t count;
	timestamp_t time;

	count = MCHP_HTIMER_COUNT(0);

	if (MCHP_HTIMER_CONTROL(0) == 1) /* if > 2 sec */
		/* 0.125 sec per count */
		time.le.lo = (uint32_t)(count * 125000);
	else /* if < 2 sec */
		/* 30.5(=61/2) us per count */
		time.le.lo = (uint32_t)(count * 61 / 2);

	time.le.hi = 0;

	return time; /* in uSec */
}

/**
 * Disable and clear hibernation timer interrupt
 */
static void system_reset_htimer_alarm(void)
{
	MCHP_HTIMER_PRELOAD(0) = 0;
	MCHP_INT_SOURCE(MCHP_HTIMER_GIRQ) = MCHP_HTIMER_GIRQ_BIT(0);
}

#ifdef CONFIG_MCHP_DEEP_SLP_DEBUG
static void print_pcr_regs(void)
{
	int i;

	trace0(0, MEC, 0, "Current PCR registers");
	for (i = 0; i < 5; i++) {
		trace12(0, MEC, 0, "REG  SLP_EN[%d] = 0x%08X", i,
			MCHP_PCR_SLP_EN(i));
		trace12(0, MEC, 0, "REG CLK_REQ[%d] = 0x%08X", i,
			MCHP_PCR_CLK_REQ(i));
	}
}

static void print_ecia_regs(void)
{
	int i;

	trace0(0, MEC, 0, "Current GIRQn.Result registers");
	for (i = MCHP_INT_GIRQ_FIRST; i <= MCHP_INT_GIRQ_LAST; i++)
		trace12(0, MEC, 0, "GIRQ[%d].Result = 0x%08X", i,
			MCHP_INT_RESULT(i));
}

static void save_regs(void)
{
	int i;

	for (i = 0; i < MCHP_PCR_SLP_RST_REG_MAX; i++) {
		pcr_slp_en[i] = MCHP_PCR_SLP_EN(i);
		pcr_clk_req[i] = MCHP_PCR_CLK_REQ(i);
	}

	for (i = 0; i < MCHP_INT_GIRQ_NUM; i++)
		ecia_result[i] = MCHP_INT_RESULT(MCHP_INT_GIRQ_FIRST + i);
}

static void print_saved_regs(void)
{
	int i;

	trace0(0, BRD, 0, "Before sleep saved registers");
	for (i = 0; i < MCHP_PCR_SLP_RST_REG_MAX; i++) {
		trace12(0, BRD, 0, "PCR_SLP_EN[%d]  = 0x%08X", i,
			pcr_slp_en[i]);
		trace12(0, BRD, 0, "PCR_CLK_REQ[%d] = 0x%08X", i,
			pcr_clk_req[i]);
	}

	for (i = 0; i < MCHP_INT_GIRQ_NUM; i++)
		trace12(0, BRD, 0, "GIRQ[%d].Result = 0x%08X",
			(i + MCHP_INT_GIRQ_FIRST), ecia_result[i]);
}
#else
static __maybe_unused void print_pcr_regs(void)
{
}
static __maybe_unused void print_ecia_regs(void)
{
}
static __maybe_unused void save_regs(void)
{
}
static __maybe_unused void print_saved_regs(void)
{
}
#endif /* #ifdef CONFIG_MCHP_DEEP_SLP_DEBUG */

/**
 * This is MCHP specific and equivalent to ARM Cortex's
 * 'DeepSleep' via system control block register, CPU_SCB_SYSCTRL
 * MCHP has new SLP_ALL feature.
 * When SLP_ALL is enabled and HW sees sleep entry trigger from CPU.
 * 1. HW saves PCR.SLP_EN registers
 * 2. HW sets all PCR.SLP_EN bits to 1.
 * 3. System sleeps
 * 4. wake event wakes system
 * 5. HW restores original values of all PCR.SLP_EN registers
 * NOTE1: Current RTOS core (Cortex-M4) does not use SysTick timer.
 * We can leave code to disable it but do not re-enable on wake.
 * NOTE2: Some peripherals will not sleep until outstanding transactions
 * are complete: I2C, DMA, GPSPI, QMSPI, etc.
 * NOTE3: Security blocks do not fully implement HW sleep therefore their
 * sleep enables must be manually set/restored.
 *
 */
static void prepare_for_deep_sleep(void)
{
	/* sysTick timer */
	CPU_NVIC_ST_CTRL &= ~ST_ENABLE;
	CPU_NVIC_ST_CTRL &= ~ST_COUNTFLAG;

	CPU_NVIC_ST_CTRL &= ~ST_TICKINT; /* SYS_TICK_INT_DISABLE */

	/* Enable assertion of DeepSleep signals
	 * from the core when core enters sleep.
	 */
	CPU_SCB_SYSCTRL |= BIT(2);

	/* Stop timers */
	MCHP_TMR32_CTL(0) &= ~1;
	MCHP_TMR32_CTL(1) &= ~1;
#ifdef CONFIG_WATCHDOG_HELP
	MCHP_TMR16_CTL(0) &= ~1;
	MCHP_INT_DISABLE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);
	MCHP_INT_SOURCE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);
#endif
	MCHP_INT_DISABLE(MCHP_TMR32_GIRQ) =
		MCHP_TMR32_GIRQ_BIT(0) + MCHP_TMR32_GIRQ_BIT(1);
	MCHP_INT_SOURCE(MCHP_TMR32_GIRQ) =
		MCHP_TMR32_GIRQ_BIT(0) + MCHP_TMR32_GIRQ_BIT(1);

#ifdef CONFIG_WATCHDOG
	/* Stop watchdog */
	MCHP_WDG_CTL &= ~1;
#endif

#ifdef CONFIG_HOST_INTERFACE_ESPI
	MCHP_INT_SOURCE(22) = MCHP_INT22_WAKE_ONLY_ESPI;
	MCHP_INT_ENABLE(22) = MCHP_INT22_WAKE_ONLY_ESPI;
#else
	MCHP_INT_SOURCE(22) = MCHP_INT22_WAKE_ONLY_LPC;
	MCHP_INT_ENABLE(22) = MCHP_INT22_WAKE_ONLY_LPC;
#endif

#ifdef CONFIG_ADC
	/*
	 * Clear ADC activate bit. If a conversion is in progress the
	 * ADC block will not enter low power until the conversion is
	 * complete.
	 */
	MCHP_ADC_CTRL &= ~1;
#endif

	/* stop Port80 capture timer */
#ifndef CHIP_FAMILY_MEC172X
	MCHP_P80_ACTIVATE(0) = 0;
#endif

	/*
	 * Clear SLP_EN bit(s) for wake sources.
	 * Currently only Hibernation timer 0.
	 * GPIO pins can always wake.
	 */
	MCHP_PCR_SLP_EN3 &= ~(MCHP_PCR_SLP_EN3_HTMR0);

#ifdef CONFIG_PWM
	pwm_keep_awake(); /* clear sleep enables of active PWM's */
#else
	/* Disable 100 Khz clock */
	MCHP_PCR_SLOW_CLK_CTL &= 0xFFFFFC00;
#endif

#ifdef CONFIG_CHIPSET_DEBUG
	/* Disable JTAG and preserve mode */
	MCHP_EC_JTAG_EN &= ~(MCHP_JTAG_ENABLE);
#endif

	/* call board level */
#ifdef CONFIG_BOARD_DEEP_SLEEP
	board_prepare_for_deep_sleep();
#endif

#ifdef CONFIG_MCHP_DEEP_SLP_DEBUG
	save_regs();
#endif
}

static void resume_from_deep_sleep(void)
{
	MCHP_PCR_SYS_SLP_CTL = 0x00; /* default */

	/* Disable assertion of DeepSleep signal when core executes WFI */
	CPU_SCB_SYSCTRL &= ~BIT(2);

#ifdef CONFIG_MCHP_DEEP_SLP_DEBUG
	print_saved_regs();
	print_pcr_regs();
	print_ecia_regs();
#endif

#ifdef CONFIG_CHIPSET_DEBUG
	MCHP_EC_JTAG_EN |= (MCHP_JTAG_ENABLE);
#endif

	MCHP_PCR_SLOW_CLK_CTL |= 0x1e0;

	/* call board level */
#ifdef CONFIG_BOARD_DEEP_SLEEP
	board_resume_from_deep_sleep();
#endif
	/*
	 * re-enable hibernation timer 0 PCR.SLP_EN to
	 * reduce power.
	 */
	MCHP_PCR_SLP_EN3 |= (MCHP_PCR_SLP_EN3_HTMR0);

#ifdef CONFIG_HOST_INTERFACE_ESPI
#ifdef CONFIG_POWER_S0IX
	MCHP_INT_DISABLE(22) = MCHP_INT22_WAKE_ONLY_ESPI;
	MCHP_INT_SOURCE(22) = MCHP_INT22_WAKE_ONLY_ESPI;
#else
	MCHP_ESPI_ACTIVATE |= 1;
#endif
#else
#ifdef CONFIG_POWER_S0IX
	MCHP_INT_DISABLE(22) = MCHP_INT22_WAKE_ONLY_LPC;
	MCHP_INT_SOURCE(22) = MCHP_INT22_WAKE_ONLY_LPC;
#else
	MCHP_LPC_ACT |= 1;
#endif
#endif

	/* re-enable Port 80 capture */
#ifndef CHIP_FAMILY_MEC172X
	MCHP_P80_ACTIVATE(0) = 1;
#endif

#ifdef CONFIG_ADC
	MCHP_ADC_CTRL |= 1;
#endif

	/* Enable timer */
	MCHP_TMR32_CTL(0) |= 1;
	MCHP_TMR32_CTL(1) |= 1;
	MCHP_TMR16_CTL(0) |= 1;
	MCHP_INT_ENABLE(MCHP_TMR32_GIRQ) =
		MCHP_TMR32_GIRQ_BIT(0) + MCHP_TMR32_GIRQ_BIT(1);
	MCHP_INT_ENABLE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);

	/* Enable watchdog */
#ifdef CONFIG_WATCHDOG
#ifdef CONFIG_CHIPSET_DEBUG
	/* enable WDG stall on active JTAG and do not start */
	MCHP_WDG_CTL = BIT(4);
#else
	MCHP_WDG_CTL |= 1;
#endif
#endif
}

void clock_refresh_console_in_use(void)
{
	disable_sleep(SLEEP_MASK_CONSOLE);

	/* Set console in use expire time. */
	console_expire_time = get_time();
	console_expire_time.val += console_in_use_timeout_sec * SECOND;
}

/**
 * Low power idle task. Executed when no tasks are ready to be scheduled.
 */
void __idle(void)
{
	timestamp_t t0;
	timestamp_t t1;
	timestamp_t ht_t1;
	uint32_t next_delay;
	uint32_t max_sleep_time;
	int time_for_dsleep;
	int uart_ready_for_deepsleep;

	htimer_init(); /* hibernation timer initialize */

	disable_sleep(SLEEP_MASK_CONSOLE);
	console_expire_time.val =
		get_time().val + CONFIG_CONSOLE_IN_USE_ON_BOOT_TIME;

	/*
	 * Print when the idle task starts. This is the lowest priority
	 * task, so this only starts once all other tasks have gotten a
	 * chance to do their task initializations and have gone to sleep.
	 */
	CPRINTS("MEC low power idle task started");

	while (1) {
		/* Disable interrupts */
		interrupt_disable();

		t0 = get_time(); /* uSec */

		/* __hw_clock_event_get() is next programmed timer event */
		next_delay = __hw_clock_event_get() - t0.le.lo;

		time_for_dsleep = next_delay > (HEAVY_SLEEP_RECOVER_TIME_USEC +
						SET_HTIMER_DELAY_USEC);

		max_sleep_time = next_delay - HEAVY_SLEEP_RECOVER_TIME_USEC;

		/* check if there enough time for deep sleep */
		if (DEEP_SLEEP_ALLOWED && time_for_dsleep) {
			/*
			 * Check if the console use has expired and
			 * console sleep is masked by GPIO(UART-RX)
			 * interrupt.
			 */
			if ((sleep_mask & SLEEP_MASK_CONSOLE) &&
			    t0.val > console_expire_time.val) {
				/* allow console to sleep. */
				enable_sleep(SLEEP_MASK_CONSOLE);

				/*
				 * Wait one clock before checking if
				 * heavy sleep is allowed to give time
				 * for sleep mask to be updated.
				 */
				clock_wait_cycles(1);

				if (LOW_SPEED_DEEP_SLEEP_ALLOWED)
					CPRINTS("MEC Disable console "
						"in deep sleep");
			}

			/* UART is not being used  */
			uart_ready_for_deepsleep =
				LOW_SPEED_DEEP_SLEEP_ALLOWED &&
				!uart_tx_in_progress() && uart_buffer_empty();

			/*
			 * Since MCHP's heavy sleep mode requires all
			 * blocks to be sleep capable, UART/console
			 * readiness is final decision factor of
			 * heavy sleep of EC.
			 */
			if (uart_ready_for_deepsleep) {
				idle_dsleep_cnt++;

				/*
				 * configure UART Rx as GPIO wakeup
				 * interrupt source
				 */
				uart_enter_dsleep();

				/* MCHP specific deep-sleep mode */
				prepare_for_deep_sleep();

				/*
				 * 'max_sleep_time' value should be big
				 * enough so that hibernation timer's
				 * interrupt triggers only after 'wfi'
				 * completes its execution.
				 */
				max_sleep_time -= (get_time().le.lo - t0.le.lo);

				/* setup/enable htimer wakeup interrupt */
				system_set_htimer_alarm(0, max_sleep_time);

				/* set sleep all just before WFI */
				MCHP_PCR_SYS_SLP_CTL |= MCHP_PCR_SYS_SLP_HEAVY;
				MCHP_PCR_SYS_SLP_CTL |= MCHP_PCR_SYS_SLP_ALL;

			} else {
				idle_sleep_cnt++;
			}

			/* Wait for interrupt: goes into deep sleep. */
			asm("dsb");
			cpu_enter_suspend_mode();
			asm("isb");
			asm("nop");

			if (uart_ready_for_deepsleep) {
				resume_from_deep_sleep();

				/*
				 * Fast forward timer according to htimer
				 * counter:
				 * Since all blocks including timers
				 * will be in sleep mode, timers stops
				 * except hibernate timer.
				 * And system schedule timer should be
				 * corrected after wakeup by either
				 * hibernate timer or GPIO_UART_RX
				 * interrupt.
				 */
				ht_t1 = system_get_htimer();

				/* disable/clear htimer wakeup interrupt */
				system_reset_htimer_alarm();

				t1.val = t0.val + (uint64_t)(max_sleep_time -
							     ht_t1.le.lo);

				force_time(t1);

				/* re-enable UART */
				uart_exit_dsleep();

				/* Record time spent in deep sleep. */
				total_idle_dsleep_time_us +=
					(uint64_t)(max_sleep_time -
						   ht_t1.le.lo);
			}

		} else { /* CPU 'Sleep' mode */

			idle_sleep_cnt++;

			cpu_enter_suspend_mode();
		}

		interrupt_enable();
	} /* while(1) */
}

#ifdef CONFIG_CMD_IDLE_STATS
/**
 * Print low power idle statistics
 */

static int command_idle_stats(int argc, const char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);

	ccprintf("Total Time spent in deep-sleep(sec): %.6lld(s)\n",
		 total_idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6llds\n\n", ts.val);

	if (IS_ENABLED(CONFIG_MCHP_DEEP_SLP_DEBUG))
		print_pcr_regs(); /* debug */

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");
#endif /* defined(CONFIG_CMD_IDLE_STATS) */

/**
 * Configure deep sleep clock settings.
 */
static int command_dsleep(int argc, const char **argv)
{
	int v;

	if (argc > 1) {
		if (parse_bool(argv[1], &v)) {
			/*
			 * Force deep sleep not to use heavy sleep mode or
			 * allow it to use the heavy sleep mode.
			 */
			if (v) /* 'on' */
				disable_sleep(SLEEP_MASK_FORCE_NO_LOW_SPEED);
			else /* 'off' */
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

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	dsleep, command_dsleep, "[ on | off | <timeout> sec]",
	"Deep sleep clock settings:\nUse 'on' to force deep "
	"sleep NOT to enter heavy sleep mode.\nUse 'off' to "
	"allow deep sleep to use heavy sleep whenever conditions "
	"allow.\n"
	"Give a timeout value for the console in use timeout.\n"
	"See also 'sleep mask'.");
#endif /* CONFIG_LOW_POWER_IDLE */

test_mockable void clock_enable_module(enum module_id module, int enable)
{
}
