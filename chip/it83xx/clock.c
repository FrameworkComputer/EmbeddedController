/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "intc.h"
#include "irq_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros. */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

#ifdef CONFIG_LOW_POWER_IDLE
#define SLEEP_SET_HTIMER_DELAY_USEC 250
#define SLEEP_FTIMER_SKIP_USEC      (HOOK_TICK_INTERVAL * 2)

static timestamp_t sleep_mode_t0;
static timestamp_t sleep_mode_t1;
static int idle_doze_cnt;
static int idle_sleep_cnt;
static uint64_t total_idle_sleep_time_us;
static int allow_sleep;
static uint32_t ec_sleep;
/*
 * Fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the heavy sleep mode is not used.
 */
#define CONSOLE_IN_USE_ON_BOOT_TIME (15*SECOND)
static int console_in_use_timeout_sec = 5;
static timestamp_t console_expire_time;

/* clock source is 32.768KHz */
#define TIMER_32P768K_CNT_TO_US(cnt) ((uint64_t)(cnt) * 1000000 / 32768)
#define TIMER_CNT_8M_32P768K(cnt)    (((cnt) / (8000000 / 32768)) + 1)
#endif /*CONFIG_LOW_POWER_IDLE */

static int freq;

struct clock_gate_ctrl {
	volatile uint8_t *reg;
	uint8_t mask;
};

static void clock_module_disable(void)
{
	/* bit0: FSPI interface tri-state */
	IT83XX_SMFI_FLHCTRL3R |= (1 << 0);
	/* bit7: USB pad power-on disable */
	IT83XX_GCTRL_PMER2 &= ~(1 << 7);
	clock_disable_peripheral((CGC_OFFSET_EGPC | CGC_OFFSET_CIR), 0, 0);
	clock_disable_peripheral((CGC_OFFSET_SMBA | CGC_OFFSET_SMBB |
		CGC_OFFSET_SMBC | CGC_OFFSET_SMBD | CGC_OFFSET_SMBE |
		CGC_OFFSET_SMBF), 0, 0);
	clock_disable_peripheral((CGC_OFFSET_SSPI | CGC_OFFSET_PECI |
		CGC_OFFSET_USB), 0, 0);
}

enum pll_freq_idx {
	PLL_24_MHZ = 1,
	PLL_48_MHZ = 2,
	PLL_96_MHZ = 4,
};

static const uint8_t pll_to_idx[8] = {
	0,
	0,
	PLL_24_MHZ,
	0,
	PLL_48_MHZ,
	0,
	0,
	PLL_96_MHZ
};

struct clock_pll_t {
	int     pll_freq;
	uint8_t pll_setting;
	uint8_t div_fnd;
	uint8_t div_uart;
	uint8_t div_usb;
	uint8_t div_smb;
	uint8_t div_sspi;
	uint8_t div_ec;
	uint8_t div_jtag;
	uint8_t div_pwm;
	uint8_t div_usbpd;
};

const struct clock_pll_t clock_pll_ctrl[] = {
	/*
	 * UART:  24MHz
	 * SMB:   24MHz
	 * EC:     8MHz
	 * JTAG:  24MHz
	 * USBPD:  8MHz
	 * USB:   48MHz(no support if PLL=24MHz)
	 * SSPI:  48MHz(24MHz if PLL=24MHz)
	 */
	/* PLL:24MHz, MCU:24MHz, Fnd(e-flash):24MHz */
	[PLL_24_MHZ] = {24000000, 2, 0, 0, 0, 0, 0, 2, 0, 0, 0x2},
	/* PLL:48MHz, MCU:48MHz, Fnd:24MHz */
	[PLL_48_MHZ] = {48000000, 4, 1, 1, 0, 1, 0, 2, 1, 0, 0x5},
	/* PLL:96MHz, MCU:96MHz, Fnd:32MHz */
	[PLL_96_MHZ] = {96000000, 7, 2, 3, 1, 3, 1, 4, 3, 1, 0xb},
};

static uint8_t pll_div_fnd;
static uint8_t pll_div_ec;
static uint8_t pll_div_jtag;
static uint8_t pll_setting;

void __ram_code clock_ec_pll_ctrl(enum ec_pll_ctrl mode)
{
	IT83XX_ECPM_PLLCTRL = mode;
	/* for deep doze / sleep mode */
	IT83XX_ECPM_PLLCTRL = mode;
	asm volatile ("dsb");
}

void __ram_code clock_pll_changed(void)
{
	IT83XX_GCTRL_SSCR &= ~(1 << 0);
	/*
	 * Update PLL settings.
	 * Writing data to this register doesn't change the
	 * PLL frequency immediately until the status is changed
	 * into wakeup from the sleep mode.
	 * The following code is intended to make the system
	 * enter sleep mode, and set up a HW timer to wakeup EC to
	 * complete PLL update.
	 */
	IT83XX_ECPM_PLLFREQR = pll_setting;
	/* Pre-set FND clock frequency = PLL / 3 */
	IT83XX_ECPM_SCDCR0 = (2 << 4);
	/* JTAG and EC */
	IT83XX_ECPM_SCDCR3 = (pll_div_jtag << 4) | pll_div_ec;
	/* EC sleep after standby instruction */
	clock_ec_pll_ctrl(EC_PLL_SLEEP);
	/* Global interrupt enable */
	asm volatile ("setgie.e");
	/* EC sleep */
	asm("standby wake_grant");
	/* Global interrupt disable */
	asm volatile ("setgie.d");
	/* New FND clock frequency */
	IT83XX_ECPM_SCDCR0 = (pll_div_fnd << 4);
	/* EC doze after standby instruction */
	clock_ec_pll_ctrl(EC_PLL_DOZE);
}

/* NOTE: Don't use this function in other place. */
static void clock_set_pll(enum pll_freq_idx idx)
{
	int pll;

	pll_div_fnd  = clock_pll_ctrl[idx].div_fnd;
	pll_div_ec   = clock_pll_ctrl[idx].div_ec;
	pll_div_jtag = clock_pll_ctrl[idx].div_jtag;
	pll_setting  = clock_pll_ctrl[idx].pll_setting;

	/* Update PLL settings or not */
	if (((IT83XX_ECPM_PLLFREQR & 0xf) != pll_setting) ||
		((IT83XX_ECPM_SCDCR0 & 0xf0) != (pll_div_fnd << 4)) ||
		((IT83XX_ECPM_SCDCR3 & 0xf) != pll_div_ec)) {
		/* Enable hw timer to wakeup EC from the sleep mode */
		ext_timer_ms(LOW_POWER_EXT_TIMER, EXT_PSR_32P768K_HZ,
				1, 1, 5, 1, 0);
		task_clear_pending_irq(et_ctrl_regs[LOW_POWER_EXT_TIMER].irq);
		/* Update PLL settings. */
		clock_pll_changed();
	}

	/* Get new/current setting of PLL frequency */
	pll = pll_to_idx[IT83XX_ECPM_PLLFREQR & 0xf];
	/* USB and UART */
	IT83XX_ECPM_SCDCR1 = (clock_pll_ctrl[pll].div_usb << 4) |
				clock_pll_ctrl[pll].div_uart;
	/* SSPI and SMB */
	IT83XX_ECPM_SCDCR2 = (clock_pll_ctrl[pll].div_sspi << 4) |
				clock_pll_ctrl[pll].div_smb;
	/* USBPD and PWM */
	IT83XX_ECPM_SCDCR4 = (clock_pll_ctrl[pll].div_usbpd << 4) |
				clock_pll_ctrl[pll].div_pwm;
	/* Current PLL frequency  */
	freq = clock_pll_ctrl[pll].pll_freq;
}

void clock_init(void)
{
	uint32_t image_type = (uint32_t)clock_init;

	/* To change interrupt vector base if at RW image */
	if (image_type > CONFIG_RW_MEM_OFF)
		/* Interrupt Vector Table Base Address, in 64k Byte unit */
		IT83XX_GCTRL_IVTBAR = (CONFIG_RW_MEM_OFF >> 16) & 0xFF;

#if (PLL_CLOCK == 24000000)     || \
	(PLL_CLOCK == 48000000) || \
	(PLL_CLOCK == 96000000)
	/* Set PLL frequency */
	clock_set_pll(PLL_CLOCK / 24000000);
#else
#error "Support only for PLL clock speed of 24/48/96MHz."
#endif
	/*
	 * The VCC power status is treated as power-on.
	 * The VCC supply of LPC and related functions (EC2I,
	 * KBC, SWUC, PMC, CIR, SSPI, UART, BRAM, and PECI).
	 * It means VCC (pin 11) should be logic high before using
	 * these functions, or firmware treats VCC logic high
	 * as following setting.
	 */
	IT83XX_GCTRL_RSTS = (IT83XX_GCTRL_RSTS & 0x3F) + 0x40;

	/* Turn off auto clock gating. */
	IT83XX_ECPM_AUTOCG = 0x00;

	/* Default doze mode */
	clock_ec_pll_ctrl(EC_PLL_DOZE);

	clock_module_disable();

#ifdef CONFIG_LPC
	IT83XX_WUC_WUESR4 = (1 << 2);
	task_clear_pending_irq(IT83XX_IRQ_WKINTAD);
	/* bit2, wake-up enable for LPC access */
	IT83XX_WUC_WUENR4 |= (1 << 2);
#endif
}

int clock_get_freq(void)
{
	return freq;
}

/**
 * Enable clock to specified peripheral
 *
 * @param offset Should be element of clock_gate_offsets enum.
 *               Bits 8-15 specify the ECPM offset of the specific clock reg.
 *               Bits 0-7 specify the mask for the clock register.
 * @param mask   Unused
 * @param mode   Unused
 */
void clock_enable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	volatile uint8_t *reg = (volatile uint8_t *)
			(IT83XX_ECPM_BASE + (offset >> 8));
	uint8_t reg_mask = offset & 0xff;

	/*
	 * Note: CGCTRL3R, bit 6, must always write 1, but since there is no
	 * offset argument that addresses this bit, then we are guaranteed
	 * that this line will write a 1 to that bit.
	 */
	*reg &= ~reg_mask;
}

/**
 * Disable clock to specified peripheral
 *
 * @param offset Should be element of clock_gate_offsets enum.
 *               Bits 8-15 specify the ECPM offset of the specific clock reg.
 *               Bits 0-7 specify the mask for the clock register.
 * @param mask   Unused
 * @param mode   Unused
 */
void clock_disable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	volatile uint8_t *reg = (volatile uint8_t *)
			(IT83XX_ECPM_BASE + (offset >> 8));
	uint8_t reg_mask = offset & 0xff;
	uint8_t tmp_mask = 0;

	/* CGCTRL3R, bit 6, must always write a 1. */
	tmp_mask |= ((offset >> 8) == IT83XX_ECPM_CGCTRL3R_OFF) ? 0x40 : 0x00;

	*reg |= reg_mask | tmp_mask;
}

#ifdef CONFIG_LOW_POWER_IDLE
void clock_refresh_console_in_use(void)
{
	/* Set console in use expire time. */
	console_expire_time = get_time();
	console_expire_time.val += console_in_use_timeout_sec * SECOND;
}

static void clock_event_timer_clock_change(enum ext_timer_clock_source clock,
					uint32_t count)
{
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) &= ~(1 << 0);
	IT83XX_ETWD_ETXPSR(EVENT_EXT_TIMER) = clock;
	IT83XX_ETWD_ETXCNTLR(EVENT_EXT_TIMER) = count;
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) |= 0x3;
}

static void clock_htimer_enable(void)
{
	uint32_t c;

	/* change event timer clock source to 32.768 KHz */
#if 0
	c = TIMER_CNT_8M_32P768K(IT83XX_ETWD_ETXCNTOR(EVENT_EXT_TIMER));
#else
	/* TODO(crosbug.com/p/55044) */
	c = TIMER_CNT_8M_32P768K(ext_observation_reg_read(EVENT_EXT_TIMER));
#endif
	clock_event_timer_clock_change(EXT_PSR_32P768K_HZ, c);
}

static int clock_allow_low_power_idle(void)
{
	if (!(IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) & (1 << 0)))
		return 0;

	if (*et_ctrl_regs[EVENT_EXT_TIMER].isr &
		et_ctrl_regs[EVENT_EXT_TIMER].mask)
		return 0;

#if 0
	if (EVENT_TIMER_COUNT_TO_US(IT83XX_ETWD_ETXCNTOR(EVENT_EXT_TIMER)) <
#else
	/* TODO(crosbug.com/p/55044) */
	if (EVENT_TIMER_COUNT_TO_US(ext_observation_reg_read(EVENT_EXT_TIMER)) <
#endif
		SLEEP_SET_HTIMER_DELAY_USEC)
		return 0;

	sleep_mode_t0 = get_time();
	if ((sleep_mode_t0.le.lo > (0xffffffff - SLEEP_FTIMER_SKIP_USEC)) ||
		(sleep_mode_t0.le.lo < SLEEP_FTIMER_SKIP_USEC))
		return 0;

	if (sleep_mode_t0.val < console_expire_time.val)
		return 0;

	return 1;
}

int clock_ec_wake_from_sleep(void)
{
	return ec_sleep;
}

void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
{
	int i;

	/* disable all interrupts */
	interrupt_disable();
	for (i = 0; i < IT83XX_IRQ_COUNT; i++) {
		chip_disable_irq(i);
		chip_clear_pending_irq(i);
	}
	/* bit5: watchdog is disabled. */
	IT83XX_ETWD_ETWCTRL |= (1 << 5);
	/* Setup GPIOs for hibernate */
	if (board_hibernate_late)
		board_hibernate_late();

	if (seconds || microseconds) {
		/* At least 1 ms for hibernate. */
		uint64_t c = (seconds * 1000 + microseconds / 1000 + 1) * 1024;

		uint64divmod(&c, 1000);
		/* enable a 56-bit timer and clock source is 1.024 KHz */
		ext_timer_stop(FREE_EXT_TIMER_L, 1);
		ext_timer_stop(FREE_EXT_TIMER_H, 1);
		IT83XX_ETWD_ETXPSR(FREE_EXT_TIMER_L) = EXT_PSR_1P024K_HZ;
		IT83XX_ETWD_ETXPSR(FREE_EXT_TIMER_H) = EXT_PSR_1P024K_HZ;
		IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_L) = c & 0xffffff;
		IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) = (c >> 24) & 0xffffffff;
		ext_timer_start(FREE_EXT_TIMER_H, 1);
		ext_timer_start(FREE_EXT_TIMER_L, 0);
	}

	for (i = 0; i < hibernate_wake_pins_used; ++i)
		gpio_enable_interrupt(hibernate_wake_pins[i]);

	/* EC sleep */
	ec_sleep = 1;
	clock_ec_pll_ctrl(EC_PLL_SLEEP);
	interrupt_enable();
	/* standby instruction */
	asm("standby wake_grant");

	/* we should never reach that point */
	while (1)
		;
}

void clock_sleep_mode_wakeup_isr(void)
{
	uint32_t st_us, c;

	/* trigger a reboot if wake up EC from sleep mode (system hibernate) */
	if (clock_ec_wake_from_sleep())
		system_reset(SYSTEM_RESET_HARD);

	if (IT83XX_ECPM_PLLCTRL == EC_PLL_DEEP_DOZE) {
		clock_ec_pll_ctrl(EC_PLL_DOZE);

		/* update free running timer */
		c = 0xffffffff - IT83XX_ETWD_ETXCNTOR(LOW_POWER_EXT_TIMER);
		st_us = TIMER_32P768K_CNT_TO_US(c);
		sleep_mode_t1.val = sleep_mode_t0.val + st_us;
		__hw_clock_source_set(sleep_mode_t1.le.lo);

		/* reset event timer and clock source is 8 MHz */
		clock_event_timer_clock_change(EXT_PSR_8M_HZ, 0xffffffff);
		task_clear_pending_irq(et_ctrl_regs[EVENT_EXT_TIMER].irq);
		process_timers(0);
#ifdef CONFIG_LPC
		/* disable lpc access wui */
		task_disable_irq(IT83XX_IRQ_WKINTAD);
		IT83XX_WUC_WUESR4 = (1 << 2);
		task_clear_pending_irq(IT83XX_IRQ_WKINTAD);
#endif
		/* disable uart wui */
		uart_exit_dsleep();
		/* Record time spent in sleep. */
		total_idle_sleep_time_us += st_us;
	}
}

/**
 * Low power idle task. Executed when no tasks are ready to be scheduled.
 */
void __idle(void)
{
	console_expire_time.val = get_time().val + CONSOLE_IN_USE_ON_BOOT_TIME;
	/* init hw timer and clock source is 32.768 KHz */
	ext_timer_ms(LOW_POWER_EXT_TIMER, EXT_PSR_32P768K_HZ, 1, 0,
		0xffffffff, 1, 1);

	/*
	 * Print when the idle task starts.  This is the lowest priority task,
	 * so this only starts once all other tasks have gotten a chance to do
	 * their task inits and have gone to sleep.
	 */
	CPRINTS("low power idle task started");

	while (1) {
		allow_sleep = 0;
		if (DEEP_SLEEP_ALLOWED)
			allow_sleep = clock_allow_low_power_idle();

		if (allow_sleep) {
			interrupt_disable();
			/* reset low power mode hw timer */
			IT83XX_ETWD_ETXCTRL(LOW_POWER_EXT_TIMER) |= (1 << 1);
			sleep_mode_t0 = get_time();
#ifdef CONFIG_LPC
			/* enable lpc access wui */
			task_enable_irq(IT83XX_IRQ_WKINTAD);
#endif
			/* enable uart wui */
			uart_enter_dsleep();
			/* enable hw timer for deep doze / sleep mode wake-up */
			clock_htimer_enable();
			/* deep doze mode */
			clock_ec_pll_ctrl(EC_PLL_DEEP_DOZE);
			interrupt_enable();
			/* standby instruction */
			asm("standby wake_grant");
			idle_sleep_cnt++;
		} else {
			/* doze mode */
			clock_ec_pll_ctrl(EC_PLL_DOZE);
			/* standby instruction */
			asm("standby wake_grant");
			idle_doze_cnt++;
		}
	}
}
#endif /* CONFIG_LOW_POWER_IDLE */

#ifdef CONFIG_LOW_POWER_IDLE
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that doze:            %d\n", idle_doze_cnt);
	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);

	ccprintf("Total Time spent in sleep(sec):      %.6ld(s)\n",
						total_idle_sleep_time_us);
	ccprintf("Total time on:                       %.6lds\n\n", ts.val);
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
			 * Force deep sleep not to use heavy sleep mode or
			 * allow it to use the heavy sleep mode.
			 */
			if (v)  /* 'on' */
				disable_sleep(SLEEP_MASK_FORCE_NO_LOW_SPEED);
			else    /* 'off' */
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

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dsleep, command_dsleep,
		"[ on | off | <timeout> sec]",
		"Deep sleep clock settings:\n"
		"Use 'on' to force deep sleep NOT to enter heavysleep mode.\n"
		"Use 'off' to allow deep sleep to use heavysleep whenever\n"
		"conditions allow.\n"
		"Give a timeout value for the console in use timeout.\n"
		"See also 'sleepmask'.");
#endif /* CONFIG_LOW_POWER_IDLE */
