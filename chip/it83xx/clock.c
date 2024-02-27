/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "adc_chip.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "intc.h"
#include "irq_chip.h"
#include "it83xx_pd.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros. */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

#ifdef CONFIG_LOW_POWER_IDLE
#define SLEEP_SET_HTIMER_DELAY_USEC 250
#define SLEEP_FTIMER_SKIP_USEC (HOOK_TICK_INTERVAL * 2)

static timestamp_t sleep_mode_t0;
static timestamp_t sleep_mode_t1;
static int idle_doze_cnt;
static int idle_sleep_cnt;
static uint64_t total_idle_sleep_time_us;
static uint32_t ec_sleep;
/*
 * Fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the heavy sleep mode is not used.
 */
static int console_in_use_timeout_sec = 5;
static timestamp_t console_expire_time;

/* clock source is 32.768KHz */
#define TIMER_32P768K_CNT_TO_US(cnt) ((uint64_t)(cnt) * 1000000 / 32768)
#define TIMER_CNT_8M_32P768K(cnt) (((cnt) / (8000000 / 32768)) + 1)
#endif /*CONFIG_LOW_POWER_IDLE */

static int freq;

struct clock_gate_ctrl {
	volatile uint8_t *reg;
	uint8_t mask;
};

static void clock_module_disable(void)
{
	/* bit0: FSPI interface tri-state */
	IT83XX_SMFI_FLHCTRL3R |= BIT(0);
	/* bit7: USB pad power-on disable */
	IT83XX_GCTRL_PMER2 &= ~BIT(7);
	/* bit7: USB debug disable */
	IT83XX_GCTRL_MCCR &= ~BIT(7);
	clock_disable_peripheral((CGC_OFFSET_EGPC | CGC_OFFSET_CIR), 0, 0);
	clock_disable_peripheral((CGC_OFFSET_SMBA | CGC_OFFSET_SMBB |
				  CGC_OFFSET_SMBC | CGC_OFFSET_SMBD |
				  CGC_OFFSET_SMBE | CGC_OFFSET_SMBF),
				 0, 0);
	clock_disable_peripheral(
		(CGC_OFFSET_SSPI | CGC_OFFSET_PECI | CGC_OFFSET_USB), 0, 0);
}

enum pll_freq_idx {
	PLL_24_MHZ = 1,
	PLL_48_MHZ = 2,
	PLL_96_MHZ = 4,
};

static const uint8_t pll_to_idx[8] = { 0,	   0, PLL_24_MHZ, 0,
				       PLL_48_MHZ, 0, 0,	  PLL_96_MHZ };

struct clock_pll_t {
	int pll_freq;
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
	[PLL_24_MHZ] = { 24000000, 2, 0, 0, 0, 0, 0, 2, 0, 0, 0x2 },
#ifdef CONFIG_IT83XX_FLASH_CLOCK_48MHZ
	/* PLL:48MHz, MCU:48MHz, Fnd:48MHz */
	[PLL_48_MHZ] = { 48000000, 4, 0, 1, 0, 1, 0, 6, 1, 0, 0x5 },
	/* PLL:96MHz, MCU:96MHz, Fnd:48MHz */
	[PLL_96_MHZ] = { 96000000, 7, 1, 3, 1, 3, 1, 6, 3, 1, 0xb },
#else
	/* PLL:48MHz, MCU:48MHz, Fnd:24MHz */
	[PLL_48_MHZ] = { 48000000, 4, 1, 1, 0, 1, 0, 2, 1, 0, 0x5 },
	/* PLL:96MHz, MCU:96MHz, Fnd:32MHz */
	[PLL_96_MHZ] = { 96000000, 7, 2, 3, 1, 3, 1, 4, 3, 1, 0xb },
#endif
};

static uint8_t pll_div_fnd;
static uint8_t pll_div_ec;
static uint8_t pll_div_jtag;
static uint8_t pll_setting;

void __ram_code clock_ec_pll_ctrl(enum ec_pll_ctrl mode)
{
	volatile uint8_t _pll_ctrl __unused;

	IT83XX_ECPM_PLLCTRL = mode;
	/*
	 * for deep doze / sleep mode
	 * This load operation will ensure PLL setting is taken into
	 * control register before wait for interrupt instruction.
	 */
	_pll_ctrl = IT83XX_ECPM_PLLCTRL;

#ifdef IT83XX_CHIP_FLASH_NO_DEEP_POWER_DOWN
	/*
	 * WORKAROUND: this workaround is used to fix EC gets stuck in low power
	 * mode when WRST# is asserted.
	 *
	 * By default, flash will go into deep power down mode automatically
	 * when EC is in low power mode. But we got an issue on IT83202BX that
	 * flash won't be able to wake up correctly when WRST# is asserted
	 * under this condition.
	 * This issue might cause cold reset failure so we fix it.
	 *
	 * NOTE: this fix will increase power number about 40uA in low power
	 * mode.
	 */
	if (mode == EC_PLL_DOZE)
		IT83XX_SMFI_SMECCS &= ~IT83XX_SMFI_MASK_HOSTWA;
	else
		/*
		 * Don't send deep power down mode command to flash when EC in
		 * low power mode.
		 */
		IT83XX_SMFI_SMECCS |= IT83XX_SMFI_MASK_HOSTWA;
#endif
	/*
	 * barrier: ensure low power mode setting is taken into control
	 * register before standby instruction.
	 */
	data_serialization_barrier();
}

void __ram_code clock_pll_changed(void)
{
	IT83XX_GCTRL_SSCR &= ~BIT(0);
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
	if (IS_ENABLED(CHIP_CORE_NDS32)) {
		/* Global interrupt enable */
		asm volatile("setgie.e");
		/* EC sleep */
		asm("standby wake_grant");
		/* Global interrupt disable */
		asm volatile("setgie.d");
	} else if (IS_ENABLED(CHIP_CORE_RISCV)) {
		/* Global interrupt enable */
		asm volatile("csrsi mstatus, 0x8");
		/* EC sleep */
		asm("wfi");
		/* Global interrupt disable */
		asm volatile("csrci mstatus, 0x8");
	}
	/* New FND clock frequency */
	IT83XX_ECPM_SCDCR0 = (pll_div_fnd << 4);
	/* EC doze after standby instruction */
	clock_ec_pll_ctrl(EC_PLL_DOZE);
}

/* NOTE: Don't use this function in other place. */
static void clock_set_pll(enum pll_freq_idx idx)
{
	int pll;

	pll_div_fnd = clock_pll_ctrl[idx].div_fnd;
	pll_div_ec = clock_pll_ctrl[idx].div_ec;
	pll_div_jtag = clock_pll_ctrl[idx].div_jtag;
	pll_setting = clock_pll_ctrl[idx].pll_setting;

	/* Update PLL settings or not */
	if (((IT83XX_ECPM_PLLFREQR & 0xf) != pll_setting) ||
	    ((IT83XX_ECPM_SCDCR0 & 0xf0) != (pll_div_fnd << 4)) ||
	    ((IT83XX_ECPM_SCDCR3 & 0xf) != pll_div_ec)) {
		/* Enable hw timer to wakeup EC from the sleep mode */
		ext_timer_ms(LOW_POWER_EXT_TIMER, EXT_PSR_32P768K_HZ, 1, 1, 5,
			     1, 0);
		task_clear_pending_irq(et_ctrl_regs[LOW_POWER_EXT_TIMER].irq);
#ifdef CONFIG_HOST_INTERFACE_ESPI
		/*
		 * Workaround for (b:70537592):
		 * We have to set chip select pin as input mode in order to
		 * change PLL.
		 */
		IT83XX_GPIO_GPCRM5 = (IT83XX_GPIO_GPCRM5 & ~0xc0) | BIT(7);
#ifdef IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
		/*
		 * On DX version, we have to disable eSPI pad before changing
		 * PLL sequence or sequence will fail if CS# pin is low.
		 */
		espi_enable_pad(0);
#endif
#endif
		/* Update PLL settings. */
		clock_pll_changed();
#ifdef CONFIG_HOST_INTERFACE_ESPI
#ifdef IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
		/* Enable eSPI pad after changing PLL sequence. */
		espi_enable_pad(1);
#endif
		/* (b:70537592) Change back to ESPI CS# function. */
		IT83XX_GPIO_GPCRM5 &= ~0xc0;
#endif
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

#if (PLL_CLOCK == 24000000) || (PLL_CLOCK == 48000000) || \
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

#if defined(IT83XX_ESPI_RESET_MODULE_BY_FW) && \
	defined(CONFIG_HOST_INTERFACE_ESPI)
	/*
	 * Because we don't support eSPI HW reset function (b/111480168) on DX
	 * version, so we have to reset eSPI configurations during init to
	 * ensure Host and EC are synchronized (especially for the field of
	 * I/O mode)
	 * Since bit4 of VWCTRL2 register is enabled, the below reset routine
	 * will be able to reset pltrst# signal.
	 */
	IT83XX_ESPI_VWCTRL2 |= ESPI_PLTRST_ESPI_RESET;
	if (!system_jumped_to_this_image())
		espi_fw_reset_module();
#endif
	/* Turn off auto clock gating. */
	IT83XX_ECPM_AUTOCG = 0x00;

	/* Default doze mode */
	clock_ec_pll_ctrl(EC_PLL_DOZE);

	clock_module_disable();

#ifdef CONFIG_HOSTCMD_X86
	IT83XX_WUC_WUESR4 = BIT(2);
	task_clear_pending_irq(IT83XX_IRQ_WKINTAD);
	/* bit2, wake-up enable for LPC access */
	IT83XX_WUC_WUENR4 |= BIT(2);
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
	volatile uint8_t *reg =
		(volatile uint8_t *)(IT83XX_ECPM_BASE + (offset >> 8));
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
	volatile uint8_t *reg =
		(volatile uint8_t *)(IT83XX_ECPM_BASE + (offset >> 8));
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
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) &= ~BIT(0);
	IT83XX_ETWD_ETXPSR(EVENT_EXT_TIMER) = clock;
	IT83XX_ETWD_ETXCNTLR(EVENT_EXT_TIMER) = count;
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) |= 0x3;
}

static void clock_htimer_enable(void)
{
	uint32_t c;

	/* change event timer clock source to 32.768 KHz */
#ifdef IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
	c = TIMER_CNT_8M_32P768K(ext_observation_reg_read(EVENT_EXT_TIMER));
#else
	c = TIMER_CNT_8M_32P768K(IT83XX_ETWD_ETXCNTOR(EVENT_EXT_TIMER));
#endif
	clock_event_timer_clock_change(EXT_PSR_32P768K_HZ, c);
}

static int clock_allow_low_power_idle(void)
{
	/*
	 * Avoiding using low frequency clock run the same count as awaken in
	 * sleep mode, so don't go to sleep mode before timer reload count.
	 */
	if (!(IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) & BIT(0)))
		return 0;

	/* If timer interrupt status is set, don't go to sleep mode. */
	if (*et_ctrl_regs[EVENT_EXT_TIMER].isr &
	    et_ctrl_regs[EVENT_EXT_TIMER].mask)
		return 0;

		/*
		 * If timer is less than 250us to expire, then we don't go to
		 * sleep mode.
		 */
#ifdef IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
	if (EVENT_TIMER_COUNT_TO_US(ext_observation_reg_read(EVENT_EXT_TIMER)) <
#else
	if (EVENT_TIMER_COUNT_TO_US(IT83XX_ETWD_ETXCNTOR(EVENT_EXT_TIMER)) <
#endif
	    SLEEP_SET_HTIMER_DELAY_USEC)
		return 0;

	/*
	 * We calculate 32bit free clock overflow counts for 64bit value,
	 * if clock almost reach overflow, we don't go to sleep mode for
	 * avoiding miss overflow count.
	 */
	sleep_mode_t0 = get_time();
	if ((sleep_mode_t0.le.lo > (0xffffffff - SLEEP_FTIMER_SKIP_USEC)) ||
	    (sleep_mode_t0.le.lo < SLEEP_FTIMER_SKIP_USEC))
		return 0;

	/* If we are waked up by console, then keep awake at least 5s. */
	if (sleep_mode_t0.val < console_expire_time.val)
		return 0;

	return 1;
}

int clock_ec_wake_from_sleep(void)
{
	return ec_sleep;
}

void __ram_code clock_cpu_standby(void)
{
	/* standby instruction */
	if (IS_ENABLED(CHIP_CORE_NDS32)) {
		asm("standby wake_grant");
	} else if (IS_ENABLED(CHIP_CORE_RISCV)) {
		if (!IS_ENABLED(IT83XX_RISCV_WAKEUP_CPU_WITHOUT_INT_ENABLED))
			/*
			 * we have to enable interrupts before
			 * standby instruction on IT83202 bx version.
			 */
			interrupt_enable();

		asm("wfi");
	}
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
	IT83XX_ETWD_ETWCTRL |= BIT(5);

	/*
	 * Setup GPIOs for hibernate.  On some boards, it's possible that this
	 * may not return at all.  On those boards, power to the EC is likely
	 * being turn off entirely.
	 */
	if (board_hibernate_late) {
		/*
		 * Set reset flag in case board_hibernate_late() doesn't
		 * return.
		 */
		chip_save_reset_flags(EC_RESET_FLAG_HIBERNATE);
		board_hibernate_late();
	}

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

	if (IS_ENABLED(CONFIG_USB_PD_TCPM_ITE_ON_CHIP)) {
		/*
		 * Disable active cc and pd modules and only left Rd_5.1k (Not
		 * Rd_DB) alive in hibernate for better power consumption.
		 */
		for (i = 0; i < CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT; i++)
			it83xx_Rd_5_1K_only_for_hibernate(i);
	}

	if (IS_ENABLED(CONFIG_ADC_VOLTAGE_COMPARATOR)) {
		/*
		 * Disable all voltage comparator modules in hibernate
		 * for better power consumption.
		 */
		for (i = CHIP_VCMP0; i < CHIP_VCMP_COUNT; i++)
			vcmp_enable(i, 0);
	}

	for (i = 0; i < hibernate_wake_pins_used; ++i)
		gpio_enable_interrupt(hibernate_wake_pins[i]);

	/* EC sleep */
	ec_sleep = 1;
#if defined(IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED) && \
	defined(CONFIG_HOST_INTERFACE_ESPI)
	/* Disable eSPI pad. */
	espi_enable_pad(0);
#endif
	clock_ec_pll_ctrl(EC_PLL_SLEEP);
	interrupt_enable();
	/* standby instruction */
	clock_cpu_standby();

	/* we should never reach that point */
	__builtin_unreachable();
}

/* use data type int here not bool to get better instruction number. */
static volatile int wait_interrupt_fired;
void clock_sleep_mode_wakeup_isr(void)
{
	uint32_t st_us, c;

	/* Clear flag on each interrupt. */
	if (IS_ENABLED(CHIP_CORE_RISCV))
		wait_interrupt_fired = 0;

	/* trigger a reboot if wake up EC from sleep mode (system hibernate) */
	if (clock_ec_wake_from_sleep()) {
#if defined(IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED) && \
	defined(CONFIG_HOST_INTERFACE_ESPI)
		/*
		 * Enable eSPI pad.
		 * We will not need to enable eSPI pad here if Dx is able to
		 * enable watchdog hardware reset function. But the function is
		 * failed (b:111264984), so the following system reset is
		 * software reset (PLL setting is not reset).
		 * We will not go into the change PLL sequence on reboot if PLL
		 * setting is the same, so the operation of enabling eSPI pad we
		 * added in clock_set_pll() will not be applied.
		 */
		espi_enable_pad(1);
#endif
		system_reset(SYSTEM_RESET_HARD);
	}

	if (IT83XX_ECPM_PLLCTRL == EC_PLL_DEEP_DOZE) {
		clock_ec_pll_ctrl(EC_PLL_DOZE);
		/* update free running timer */
		c = LOW_POWER_TIMER_MASK -
		    IT83XX_ETWD_ETXCNTOR(LOW_POWER_EXT_TIMER);
		st_us = TIMER_32P768K_CNT_TO_US(c);
		sleep_mode_t1.val = sleep_mode_t0.val + st_us;
		__hw_clock_source_set(sleep_mode_t1.le.lo);

		/* reset event timer and clock source is 8 MHz */
		clock_event_timer_clock_change(EXT_PSR_8M_HZ, 0xffffffff);
		task_clear_pending_irq(et_ctrl_regs[EVENT_EXT_TIMER].irq);
		process_timers(0);
#ifdef CONFIG_HOSTCMD_X86
		/* disable lpc access wui */
		task_disable_irq(IT83XX_IRQ_WKINTAD);
		IT83XX_WUC_WUESR4 = BIT(2);
		task_clear_pending_irq(IT83XX_IRQ_WKINTAD);
#endif
		/* disable uart wui */
		uart_exit_dsleep();
		/* Record time spent in sleep. */
		total_idle_sleep_time_us += st_us;
	}
}

void __keep __idle_init(void)
{
	console_expire_time.val =
		get_time().val + CONFIG_CONSOLE_IN_USE_ON_BOOT_TIME;
	/* init hw timer and clock source is 32.768 KHz */
	ext_timer_ms(LOW_POWER_EXT_TIMER, EXT_PSR_32P768K_HZ, 1, 0, 0xffffffff,
		     1, 1);

	/*
	 * Print when the idle task starts.  This is the lowest priority task,
	 * so this only starts once all other tasks have gotten a chance to do
	 * their task inits and have gone to sleep.
	 */
	CPRINTS("low power idle task started");
}

/**
 * Low power idle task. Executed when no tasks are ready to be scheduled.
 */
void __ram_code __idle(void)
{
	/*
	 * There is not enough space from ram code section to cache entire idle
	 * function, hence pull initialization function out of the section.
	 */
	__idle_init();

	while (1) {
		/* Disable interrupts */
		interrupt_disable();
#ifdef CONFIG_IT83XX_I2C_CMD_QUEUE
		if (i2c_idle_not_allowed()) {
			interrupt_enable();
			continue;
		}
#endif
		/* Check if the EC can enter deep doze mode or not */
		if (DEEP_SLEEP_ALLOWED && clock_allow_low_power_idle()) {
			/* reset low power mode hw timer */
			IT83XX_ETWD_ETXCTRL(LOW_POWER_EXT_TIMER) |= BIT(1);
			sleep_mode_t0 = get_time();
#ifdef CONFIG_HOSTCMD_X86
			/* enable lpc access wui */
			task_enable_irq(IT83XX_IRQ_WKINTAD);
#endif
			/* enable uart wui */
			uart_enter_dsleep();
			/* enable hw timer for deep doze / sleep mode wake-up */
			clock_htimer_enable();
			/* deep doze mode */
			clock_ec_pll_ctrl(EC_PLL_DEEP_DOZE);
			idle_sleep_cnt++;
		} else {
			/* doze mode */
			clock_ec_pll_ctrl(EC_PLL_DOZE);
			idle_doze_cnt++;
		}
		/* Set flag before entering low power mode. */
		if (IS_ENABLED(CHIP_CORE_RISCV))
			wait_interrupt_fired = 1;
		clock_cpu_standby();
		interrupt_enable();
		/*
		 * Sometimes wfi instruction may fail due to CPU's MTIP@mip
		 * register is non-zero.
		 * If the wait_interrupt_fired flag is true at this point,
		 * it means that EC waked-up by the above issue not an
		 * interrupt. Hence we loop running wfi instruction here until
		 * wfi success.
		 */
		while (IS_ENABLED(CHIP_CORE_RISCV) && wait_interrupt_fired)
			clock_cpu_standby();
	}
}
#endif /* CONFIG_LOW_POWER_IDLE */

#ifdef CONFIG_LOW_POWER_IDLE
#ifdef CONFIG_CMD_IDLE_STATS
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, const char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that doze:            %d\n", idle_doze_cnt);
	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);

	ccprintf("Total Time spent in sleep(sec):      %.6lld(s)\n",
		 total_idle_sleep_time_us);
	ccprintf("Total time on:                       %.6llds\n\n", ts.val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");

#endif /* CONFIG_CMD_IDLE_STATS */
#endif /* CONFIG_LOW_POWER_IDLE */

test_mockable void clock_enable_module(enum module_id module, int enable)
{
}
