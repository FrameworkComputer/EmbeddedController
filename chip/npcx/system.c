/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : NPCX hardware specific implementation */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer_chip.h"
#include "registers.h"
#include "rom_chip.h"
#include "sib_chip.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Delay after writing TTC for value to latch */
#define MTC_TTC_LOAD_DELAY_US 250
#define MTC_ALARM_MASK     (BIT(25) - 1)
#define MTC_WUI_GROUP      MIWU_GROUP_4
#define MTC_WUI_MASK       MASK_PIN7

/* ROM address of chip revision */
#define CHIP_REV_ADDR 0x00007FFC

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/*****************************************************************************/
/* Internal functions */

void system_watchdog_reset(void)
{
	/* Unlock & stop watchdog registers */
	NPCX_WDSDM = 0x87;
	NPCX_WDSDM = 0x61;
	NPCX_WDSDM = 0x63;

	/* Reset TWCFG */
	NPCX_TWCFG = 0;
	/* Select T0IN clock as watchdog prescaler clock */
	SET_BIT(NPCX_TWCFG, NPCX_TWCFG_WDCT0I);

	/* Clear watchdog reset status initially*/
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS);

	/* Keep prescaler ratio timer0 clock to 1:1 */
	NPCX_TWCP = 0x00;

	/* Set internal counter and prescaler */
	NPCX_TWDT0 = 0x00;
	NPCX_WDCNT = 0x01;

	/* Disable interrupt */
	interrupt_disable();
	/* Reload and restart Timer 0*/
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_RST);
	/* Wait for timer is loaded and restart */
	while (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_RST))
		;
	/* Enable interrupt */
	interrupt_enable();
}

/* Return true if index is stored as a single byte in bbram */
static int bbram_is_byte_access(enum bbram_data_index index)
{
	return (index >= BBRM_DATA_INDEX_VBNVCNTXT &&
		index <  BBRM_DATA_INDEX_RAMLOG)
		|| index == BBRM_DATA_INDEX_PD0
		|| index == BBRM_DATA_INDEX_PD1
		|| index == BBRM_DATA_INDEX_PD2
		|| index == BBRM_DATA_INDEX_PANIC_FLAGS
	;
}

/* Check and clear BBRAM status on any reset */
void system_check_bbram_on_reset(void)
{
	if (IS_BIT_SET(NPCX_BKUP_STS, NPCX_BKUP_STS_IBBR)) {
		/*
		 * If the reset cause is not power-on reset and VBAT has ever
		 * dropped, print a warning message.
		 */
		if (IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_SCRATCH) ||
			IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_STS))
			CPRINTF("VBAT drop!\n");

		/*
		 * npcx5/npcx7m6g/npcx7m6f:
		 *   Clear IBBR bit
		 * npcx7m6fb/npcx7m6fc/npcx7m7wb/npcx7m7wc:
		 *   Clear IBBR/VSBY_STS/VCC1_STS bit
		 */
		NPCX_BKUP_STS = NPCX_BKUP_STS_ALL_MASK;
	}
}

/* Check index is within valid BBRAM range and IBBR is not set */
static int bbram_valid(enum bbram_data_index index, int bytes)
{
	/* Check index */
	if (index < 0 || index + bytes > NPCX_BBRAM_SIZE)
		return 0;

	/* Check BBRAM is valid */
	if (IS_BIT_SET(NPCX_BKUP_STS, NPCX_BKUP_STS_IBBR)) {
		NPCX_BKUP_STS = BIT(NPCX_BKUP_STS_IBBR);
		panic_printf("IBBR set: BBRAM corrupted!\n");
		return 0;
	}
	return 1;
}

/**
 * Read battery-backed ram (BBRAM) at specified index.
 *
 * @return The value of the register or 0 if invalid index.
 */
static uint32_t bbram_data_read(enum bbram_data_index index)
{
	uint32_t value = 0;
	int bytes = bbram_is_byte_access(index) ? 1 : 4;

	if (!bbram_valid(index, bytes))
		return 0;

	/* Read BBRAM */
	if (bytes == 4) {
		value += NPCX_BBRAM(index + 3);
		value = value << 8;
		value += NPCX_BBRAM(index + 2);
		value = value << 8;
		value += NPCX_BBRAM(index + 1);
		value = value << 8;
	}
	value += NPCX_BBRAM(index);

	return value;
}

/**
 * Write battery-backed ram (BBRAM) at specified index.
 *
 * @return nonzero if error.
 */
static int bbram_data_write(enum bbram_data_index index, uint32_t value)
{
	int bytes = bbram_is_byte_access(index) ? 1 : 4;

	if (!bbram_valid(index, bytes))
		return EC_ERROR_INVAL;

	/* Write BBRAM */
	NPCX_BBRAM(index) = value & 0xFF;
	if (bytes == 4) {
		NPCX_BBRAM(index + 1) = (value >> 8)  & 0xFF;
		NPCX_BBRAM(index + 2) = (value >> 16) & 0xFF;
		NPCX_BBRAM(index + 3) = (value >> 24) & 0xFF;
	}

	/* Wait for write-complete */
	return EC_SUCCESS;
}

/* Map idx to a returned BBRM_DATA_INDEX_*, or return -1 on invalid idx */
static int bbram_idx_lookup(enum system_bbram_idx idx)
{
	if (idx >= SYSTEM_BBRAM_IDX_VBNVBLOCK0 &&
	    idx <= SYSTEM_BBRAM_IDX_VBNVBLOCK15)
		return BBRM_DATA_INDEX_VBNVCNTXT +
		       idx - SYSTEM_BBRAM_IDX_VBNVBLOCK0;
	if (idx == SYSTEM_BBRAM_IDX_PD0)
		return BBRM_DATA_INDEX_PD0;
	if (idx == SYSTEM_BBRAM_IDX_PD1)
		return BBRM_DATA_INDEX_PD1;
	if (idx == SYSTEM_BBRAM_IDX_PD2)
		return BBRM_DATA_INDEX_PD2;
	if (idx == SYSTEM_BBRAM_IDX_TRY_SLOT)
		return BBRM_DATA_INDEX_TRY_SLOT;
	return -1;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int bbram_idx = bbram_idx_lookup(idx);

	if (bbram_idx < 0)
		return EC_ERROR_INVAL;

	*value = bbram_data_read(bbram_idx);
	return EC_SUCCESS;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	int bbram_idx = bbram_idx_lookup(idx);

	if (bbram_idx < 0)
		return EC_ERROR_INVAL;

	return bbram_data_write(bbram_idx, value);
}

/* MTC functions */
uint32_t system_get_rtc_sec(void)
{
	/* Get MTC counter unit:seconds */
	uint32_t sec = NPCX_TTC;
	return sec;
}

void system_set_rtc(uint32_t seconds)
{
	/*
	 * Set MTC counter unit:seconds, write twice to ensure values
	 * latch to NVMem.
	 */
	NPCX_TTC = seconds;
	udelay(MTC_TTC_LOAD_DELAY_US);
	NPCX_TTC = seconds;
	udelay(MTC_TTC_LOAD_DELAY_US);
}

#ifdef CONFIG_CHIP_PANIC_BACKUP
/*
 * Following information from panic data is stored in BBRAM:
 *
 * index     |       data
 * ==========|=============
 *   36      |       MMFS
 *   40      |       HFSR
 *   44      |       BFAR
 *   48      |      LREG1
 *   52      |      LREG3
 *   56      |      LREG4
 *   60      |     reserved
 *
 * Above registers are chosen to be saved in case of panic because:
 * 1. MMFS, HFSR and BFAR seem to provide more information about the fault.
 * 2. LREG1, LREG3 and LREG4 store exception, reason and info in case of
 * software panic.
 */
#define BKUP_MMFS		(BBRM_DATA_INDEX_PANIC_BKUP + 0)
#define BKUP_HFSR		(BBRM_DATA_INDEX_PANIC_BKUP + 4)
#define BKUP_BFAR		(BBRM_DATA_INDEX_PANIC_BKUP + 8)
#define BKUP_LREG1		(BBRM_DATA_INDEX_PANIC_BKUP + 12)
#define BKUP_LREG3		(BBRM_DATA_INDEX_PANIC_BKUP + 16)
#define BKUP_LREG4		(BBRM_DATA_INDEX_PANIC_BKUP + 20)

#define BKUP_PANIC_DATA_VALID	BIT(0)

void chip_panic_data_backup(void)
{
	struct panic_data *d = panic_get_data();

	if (!d)
		return;

	bbram_data_write(BKUP_MMFS, d->cm.mmfs);
	bbram_data_write(BKUP_HFSR, d->cm.hfsr);
	bbram_data_write(BKUP_BFAR, d->cm.dfsr);
	bbram_data_write(BKUP_LREG1, d->cm.regs[1]);
	bbram_data_write(BKUP_LREG3, d->cm.regs[3]);
	bbram_data_write(BKUP_LREG4, d->cm.regs[4]);
	bbram_data_write(BBRM_DATA_INDEX_PANIC_FLAGS, BKUP_PANIC_DATA_VALID);
}

static void chip_panic_data_restore(void)
{
	struct panic_data *d = PANIC_DATA_PTR;

	/* Ensure BBRAM is valid. */
	if (!bbram_valid(BKUP_MMFS, 4))
		return;

	/* Ensure Panic data in BBRAM is valid. */
	if (!(bbram_data_read(BBRM_DATA_INDEX_PANIC_FLAGS) &
	      BKUP_PANIC_DATA_VALID))
		return;

	memset(d, 0, sizeof(*d));
	d->magic = PANIC_DATA_MAGIC;
	d->struct_size = sizeof(*d);
	d->struct_version = 2;
	d->arch = PANIC_ARCH_CORTEX_M;

	d->cm.mmfs = bbram_data_read(BKUP_MMFS);
	d->cm.hfsr = bbram_data_read(BKUP_HFSR);
	d->cm.dfsr = bbram_data_read(BKUP_BFAR);

	d->cm.regs[1] = bbram_data_read(BKUP_LREG1);
	d->cm.regs[3] = bbram_data_read(BKUP_LREG3);
	d->cm.regs[4] = bbram_data_read(BKUP_LREG4);

	/* Reset panic data in BBRAM. */
	bbram_data_write(BBRM_DATA_INDEX_PANIC_FLAGS, 0);
}
#endif /* CONFIG_CHIP_PANIC_BACKUP */

void chip_save_reset_flags(uint32_t flags)
{
	bbram_data_write(BBRM_DATA_INDEX_SAVED_RESET_FLAGS, flags);
}

uint32_t chip_read_reset_flags(void)
{
	return bbram_data_read(BBRM_DATA_INDEX_SAVED_RESET_FLAGS);
}

#ifdef CONFIG_POWER_BUTTON_INIT_IDLE
/*
 * Set/clear AP_OFF flag. It's set when the system gracefully shuts down and
 * it's cleared when the system boots up. The result is the system tries to
 * go back to the previous state upon AC plug-in. If the system uncleanly
 * shuts down, it boots immediately. If the system shuts down gracefully,
 * it'll stay at S5 and wait for power button press.
 */
static void board_chipset_startup(void)
{
	uint32_t flags = bbram_data_read(BBRM_DATA_INDEX_SAVED_RESET_FLAGS);
	flags &= ~EC_RESET_FLAG_AP_OFF;
	chip_save_reset_flags(flags);
	system_clear_reset_flags(EC_RESET_FLAG_AP_OFF);
	CPRINTS("Cleared AP_OFF flag");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void board_chipset_shutdown(void)
{
	uint32_t flags = bbram_data_read(BBRM_DATA_INDEX_SAVED_RESET_FLAGS);
	flags |= EC_RESET_FLAG_AP_OFF;
	chip_save_reset_flags(flags);
	system_set_reset_flags(EC_RESET_FLAG_AP_OFF);
	CPRINTS("Set AP_OFF flag");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown,
	     /* Slightly higher than handle_pending_reboot because
	      * it may clear AP_OFF flag. */
	     HOOK_PRIO_DEFAULT - 1);
#endif

static void check_reset_cause(void)
{
	uint32_t hib_wake_flags = bbram_data_read(BBRM_DATA_INDEX_WAKE);
	uint32_t flags = bbram_data_read(BBRM_DATA_INDEX_SAVED_RESET_FLAGS);

	/* Clear saved reset flags in bbram */
#ifdef CONFIG_POWER_BUTTON_INIT_IDLE
	/* We'll clear AP_OFF on S5->S3 transition */
	chip_save_reset_flags(flags & EC_RESET_FLAG_AP_OFF);
#else
	chip_save_reset_flags(0);
#endif
	/* Clear saved hibernate wake flag in bbram , too */
	bbram_data_write(BBRM_DATA_INDEX_WAKE, 0);

	/* Use scratch bit to check power on reset or VCC1_RST reset */
	if (!IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_SCRATCH)) {
#ifdef CONFIG_BOARD_FORCE_RESET_PIN
		/* Treat all resets as RESET_PIN */
		flags |= EC_RESET_FLAG_RESET_PIN;
#else
		/* Check for VCC1 reset */
		if (IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_STS))
			flags |= EC_RESET_FLAG_RESET_PIN;
		else
			flags |= EC_RESET_FLAG_POWER_ON;
#endif
	}

	/*
	 * Set scratch bit to distinguish VCC1RST# is asserted again
	 * or not. This bit will be clear automatically when VCC1RST#
	 * is asserted or power-on reset occurs
	 */
	SET_BIT(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_SCRATCH);

	/* Software debugger reset */
	if (IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_DBGRST_STS)) {
		flags |= EC_RESET_FLAG_SOFT;
		/* Clear debugger reset status initially*/
		SET_BIT(NPCX_RSTCTL, NPCX_RSTCTL_DBGRST_STS);
	}

	/* Reset by hibernate */
	if (hib_wake_flags & HIBERNATE_WAKE_PIN)
		flags |= EC_RESET_FLAG_WAKE_PIN | EC_RESET_FLAG_HIBERNATE;
	else if (hib_wake_flags & HIBERNATE_WAKE_MTC)
		flags |= EC_RESET_FLAG_RTC_ALARM | EC_RESET_FLAG_HIBERNATE;

	/* Watchdog Reset */
	if (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS)) {
		/*
		 * Don't set EC_RESET_FLAG_WATCHDOG flag if watchdog is issued
		 * by system_reset or hibernate in order to distinguish reset
		 * cause is panic reason or not.
		 */
		if (!(flags & (EC_RESET_FLAG_SOFT | EC_RESET_FLAG_HARD |
				EC_RESET_FLAG_HIBERNATE)))
			flags |= EC_RESET_FLAG_WATCHDOG;

		/* Clear watchdog reset status initially*/
		SET_BIT(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS);
	}

	system_set_reset_flags(flags);
}

/**
 * Chip-level function to set GPIOs and wake-up inputs for hibernate.
 */
#ifdef CONFIG_SUPPORT_CHIP_HIBERNATION
static void system_set_gpios_and_wakeup_inputs_hibernate(void)
{
	int table, i;

	/* Disable all MIWU inputs before entering hibernate */
	for (table = MIWU_TABLE_0 ; table < MIWU_TABLE_2 ; table++) {
		for (i = 0 ; i < 8 ; i++) {
			/* Disable all wake-ups */
			NPCX_WKEN(table, i)  = 0x00;
			/* Clear all pending bits of wake-ups */
			NPCX_WKPCL(table, i) = 0xFF;
			/*
			 * Disable all inputs of wake-ups to prevent leakage
			 * caused by input floating.
			 */
			NPCX_WKINEN(table, i) = 0x00;
		}
	}

#if defined(CHIP_FAMILY_NPCX7)
	/* Disable MIWU 2 group 6 inputs which used for the additional GPIOs */
	NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_6)  = 0x00;
	NPCX_WKPCL(MIWU_TABLE_2, MIWU_GROUP_6) = 0xFF;
	NPCX_WKINEN(MIWU_TABLE_2, MIWU_GROUP_6) = 0x00;
#endif

	/* Enable wake-up inputs of hibernate_wake_pins array */
	for (i = 0; i < hibernate_wake_pins_used; i++) {
		gpio_reset(hibernate_wake_pins[i]);
		/* Re-enable interrupt for wake-up inputs */
		gpio_enable_interrupt(hibernate_wake_pins[i]);
#if defined(CONFIG_HIBERNATE_PSL)
		/* Config PSL pins setting for wake-up inputs */
		if (!system_config_psl_mode(hibernate_wake_pins[i]))
			ccprintf("Invalid PSL setting in wake-up pin %d\n", i);
#endif
	}
}

/**
 * hibernate function for npcx ec.
 *
 * @param seconds      Number of seconds to sleep before LCT alarm
 * @param microseconds Number of microseconds to sleep before LCT alarm
 */
void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
{
	int i;

	/* Disable ADC */
	NPCX_ADCCNF = 0;
	usleep(1000);

	/* Set SPI pins to be in Tri-State */
	SET_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);

	/* Disable instant wake up mode for better power consumption */
	CLEAR_BIT(NPCX_ENIDL_CTL, NPCX_ENIDL_CTL_LP_WK_CTL);

	/* Disable interrupt */
	interrupt_disable();

	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);
	/* ITIM time module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM32), NPCX_ITCTS_ITEN);
	/* ITIM watchdog warn module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN);

	/* Unlock & stop watchdog */
	NPCX_WDSDM = 0x87;
	NPCX_WDSDM = 0x61;
	NPCX_WDSDM = 0x63;

	/* Initialize watchdog */
	NPCX_TWCFG = 0; /* Select T0IN clock as watchdog prescaler clock */
	SET_BIT(NPCX_TWCFG, NPCX_TWCFG_WDCT0I);
	NPCX_TWCP = 0x00; /* Keep prescaler ratio timer0 clock to 1:1 */
	NPCX_TWDT0 = 0x00; /* Set internal counter and prescaler */

	/* Disable interrupt */
	interrupt_disable();

	/*
	 * Set gpios and wake-up input for better power consumption before
	 * entering hibernate.
	 */
	system_set_gpios_and_wakeup_inputs_hibernate();

	/*
	 * Give the board a chance to do any late stage hibernation work.
	 * This is likely going to configure GPIOs for hibernation.
	 */
	if (board_hibernate_late)
		board_hibernate_late();

	/* Clear all pending IRQ otherwise wfi will have no affect */
	for (i = NPCX_IRQ_0 ; i < NPCX_IRQ_COUNT ; i++)
		task_clear_pending_irq(i);

	/*
	 * Set RTC interrupt in time to wake up before
	 * next event.
	 */
	if (seconds || microseconds)
		system_set_rtc_alarm(seconds, microseconds);


	/* execute hibernate func depend on chip series */
	__hibernate_npcx_series();

}
#endif /* CONFIG_SUPPORT_CHIP_HIBERNATION */

static char system_to_hex(uint8_t x)
{
	if (x <= 9)
		return '0' + x;
	return 'a' + x - 10;
}

/*****************************************************************************/
/* IC specific low-level driver */

/*
 * Microseconds will be ignored.  The WTC register only
 * stores wakeup time in seconds.
 * Set seconds = 0 to disable the alarm
 */
void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds)
{
	uint32_t cur_secs, alarm_secs;

	if (seconds == EC_RTC_ALARM_CLEAR && !microseconds) {
		CLEAR_BIT(NPCX_WTC, NPCX_WTC_WIE);
		SET_BIT(NPCX_WTC, NPCX_WTC_PTO);

		return;
	}

	/* Get current clock */
	cur_secs = NPCX_TTC;

	/* If alarm clock is not sequential or not in range */
	alarm_secs = cur_secs + seconds;
	alarm_secs = alarm_secs & MTC_ALARM_MASK;

	/*
	 * We should set new alarm (first 25 bits of clock value) first before
	 * clearing PTO in case issue rtc interrupt immediately.
	 */
	NPCX_WTC = alarm_secs;

	/* Reset alarm first */
	system_reset_rtc_alarm();

	/* Enable interrupt mode alarm */
	SET_BIT(NPCX_WTC, NPCX_WTC_WIE);

	/* Enable MTC interrupt */
	task_enable_irq(NPCX_IRQ_MTC_WKINTAD_0);

	/* Enable wake-up input sources & clear pending bit */
	NPCX_WKPCL(MIWU_TABLE_0, MTC_WUI_GROUP)  |= MTC_WUI_MASK;
	NPCX_WKINEN(MIWU_TABLE_0, MTC_WUI_GROUP) |= MTC_WUI_MASK;
	NPCX_WKEN(MIWU_TABLE_0, MTC_WUI_GROUP)   |= MTC_WUI_MASK;
}

void system_reset_rtc_alarm(void)
{
	/*
	 * Clear interrupt & Disable alarm interrupt
	 * Update alarm value to zero
	 */
	CLEAR_BIT(NPCX_WTC, NPCX_WTC_WIE);
	SET_BIT(NPCX_WTC, NPCX_WTC_PTO);

	/* Disable MTC interrupt */
	task_disable_irq(NPCX_IRQ_MTC_WKINTAD_0);
}

/*
 * Return the seconds remaining before the RTC alarm goes off.
 * Returns 0 if alarm is not set.
 */
uint32_t system_get_rtc_alarm(void)
{
	/*
	 * Return 0:
	 * 1. If alarm is not set to go off, OR
	 * 2. If alarm is set and has already gone off
	 */
	if (!IS_BIT_SET(NPCX_WTC, NPCX_WTC_WIE) ||
	    IS_BIT_SET(NPCX_WTC, NPCX_WTC_PTO)) {
		return 0;
	}
	/* Get seconds before alarm goes off */
	return (NPCX_WTC - NPCX_TTC) & MTC_ALARM_MASK;
}

/**
 * Enable hibernate interrupt
 */
void system_enable_hib_interrupt(void)
{
	task_enable_irq(NPCX_IRQ_MTC_WKINTAD_0);
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* Flush console before hibernating */
	cflush();

	if (board_hibernate)
		board_hibernate();

#ifdef CONFIG_SUPPORT_CHIP_HIBERNATION
	/* Add additional hibernate operations here */
	__enter_hibernate(seconds, microseconds);
#endif
}

void chip_pre_init(void)
{
	/* Setting for fixing JTAG issue */
	NPCX_DBGCTRL = 0x04;
	/* Enable automatic freeze mode */
	CLEAR_BIT(NPCX_DBGFRZEN3, NPCX_DBGFRZEN3_GLBL_FRZ_DIS);

	/*
	 * Enable JTAG functionality by SW without pulling down strap-pin
	 * nJEN0 or nJEN1 during ec POWERON or VCCRST reset occurs.
	 * Please notice it will change pinmux to JTAG directly.
	 */
#ifdef NPCX_ENABLE_JTAG
#if NPCX_JTAG_MODULE2
	CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_5), NPCX_DEVALT5_NJEN1_EN);
#else
	CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_5), NPCX_DEVALT5_NJEN0_EN);
#endif
#endif

#ifndef CONFIG_ENABLE_JTAG_SELECTION
	/*
	 * (b/129908668)
	 * This is the workaround to disable the JTAG0 which is enabled
	 * accidentally by a special key combination.
	 */
	if (!IS_BIT_SET(NPCX_DEVALT(5), NPCX_DEVALT5_NJEN0_EN)) {
		int data;
		/* Set DEVALT5.nJEN0_EN to disable JTAG0 */
		SET_BIT(NPCX_DEVALT(5), NPCX_DEVALT5_NJEN0_EN);
		/* Enable Core-to-Host Modules Access */
		SET_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSAE);
		/* Clear SIOCFD.JEN0_HSL to disable JTAG0 */
		data = sib_read_reg(SIO_OFFSET, 0x2D);
		data &= ~0x80;
		sib_write_reg(SIO_OFFSET, 0x2D, data);
		/* Disable Core-to-Host Modules Access */
		CLEAR_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSAE);
	}
#endif

}

void system_pre_init(void)
{
	uint8_t pwdwn6;

	/*
	 * Add additional initialization here
	 * EC should be initialized in Booter
	 */

	/* Power-down the modules we don't need */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_1) = 0xF9; /* Skip SDP_PD FIU_PD */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_2) = 0xFF;
#if defined(CHIP_FAMILY_NPCX5)
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_3) = 0x0F; /* Skip GDMA */
#elif defined(CHIP_FAMILY_NPCX7)
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_3) = 0x1F; /* Skip GDMA */
#endif
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_4) = 0xF4; /* Skip ITIM2/1_PD */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5) = 0xF8;

	pwdwn6 = 0x70 |
		BIT(NPCX_PWDWN_CTL6_ITIM6_PD) |
		BIT(NPCX_PWDWN_CTL6_ITIM4_PD); /* Skip ITIM5_PD */
#if !defined(CONFIG_HOSTCMD_ESPI)
	pwdwn6 |= 1 << NPCX_PWDWN_CTL6_ESPI_PD;
#endif
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6) = pwdwn6;

#if defined(CHIP_FAMILY_NPCX7)
#if defined(CHIP_VARIANT_NPCX7M6FB) || defined(CHIP_VARIANT_NPCX7M6FC) || \
	defined(CHIP_VARIANT_NPCX7M7WB) || defined(CHIP_VARIANT_NPCX7M7WC)
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_7) = 0xE7;
#else
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_7) = 0x07;
#endif
#endif

	/* Following modules can be powered down automatically in npcx7 */
#if defined(CHIP_FAMILY_NPCX5)
	/* Power down the modules of npcx5 used internally */
	NPCX_INTERNAL_CTRL1 = 0x03;
	NPCX_INTERNAL_CTRL2 = 0x03;
	NPCX_INTERNAL_CTRL3 = 0x03;

	/* Enable low-power regulator */
	CLEAR_BIT(NPCX_LFCGCALCNT, NPCX_LFCGCALCNT_LPREG_CTL_EN);
	SET_BIT(NPCX_LFCGCALCNT, NPCX_LFCGCALCNT_LPREG_CTL_EN);
#endif

	/*
	 * Configure LPRAM in the MPU as a regular memory
	 * and DATA RAM to prevent code execution
	 */
	system_mpu_config();

	/*
	 * Change FMUL_WIN_DLY from 0x8A to 0x81 for better WoV
	 * audio quality.
	 */
#ifdef CHIP_FAMILY_NPCX7
	NPCX_FMUL_WIN_DLY = 0x81;
#endif

#ifdef CONFIG_CHIP_PANIC_BACKUP
	chip_panic_data_restore();
#endif
}

void system_reset(int flags)
{
	uint32_t save_flags;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/*  Get flags to be saved in BBRAM */
	system_encode_save_flags(flags, &save_flags);

	/* Store flags to battery backed RAM. */
	chip_save_reset_flags(save_flags);

	/* If WAIT_EXT is set, then allow 10 seconds for external reset */
	if (flags & SYSTEM_RESET_WAIT_EXT) {
		int i;

		/* Wait 10 seconds for external reset */
		for (i = 0; i < 1000; i++) {
			watchdog_reload();
			udelay(10000);
		}
	}

	/* Ask the watchdog to trigger a hard reboot */
	system_watchdog_reset();

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

/**
 * Return the chip vendor/name/revision string.
 */
const char *system_get_chip_vendor(void)
{
	static char str[15] = "Unknown-";
	char *p = str + 8;

	/* Read Vendor ID in core register */
	uint8_t fam_id = NPCX_SID_CR;
	switch (fam_id) {
	case 0x20:
		return "Nuvoton";
	default:
		*p       = system_to_hex((fam_id & 0xF0) >> 4);
		*(p + 1) = system_to_hex(fam_id & 0x0F);
		*(p + 2) = '\0';
		return str;
	}
}

const char *system_get_chip_name(void)
{
	static char str[15] = "Unknown-";
	char *p = str + 8;

	/* Read Chip ID in core register */
	uint8_t chip_id = NPCX_DEVICE_ID_CR;
	switch (chip_id) {
#if defined(CHIP_FAMILY_NPCX5)
	case 0x12:
		return "NPCX585G";
	case 0x13:
		return "NPCX575G";
	case 0x16:
		return "NPCX586G";
	case 0x17:
		return "NPCX576G";
#elif defined(CHIP_FAMILY_NPCX7)
	case 0x1F:
		return "NPCX787G";
	case 0x21:
	case 0x29:
		return "NPCX796F";
	case 0x24:
	case 0x2C:
		return "NPCX797W";
#endif
	default:
		*p       = system_to_hex((chip_id & 0xF0) >> 4);
		*(p + 1) = system_to_hex(chip_id & 0x0F);
		*(p + 2) = '\0';
		return str;
	}
}

const char *system_get_chip_revision(void)
{
	static char rev[6];
	char *p = rev;
	/* Read chip generation from SRID_CR */
	uint8_t chip_gen = NPCX_SRID_CR;
	/* Read ROM data for chip revision directly */
	uint8_t rev_num = *((uint8_t *)CHIP_REV_ADDR);
#ifdef CHIP_FAMILY_NPCX7
	uint8_t chip_id = NPCX_DEVICE_ID_CR;
#endif

	switch (chip_gen) {
#if defined(CHIP_FAMILY_NPCX5)
	case 0x05:
		*p++ = 'A';
		break;
#elif defined(CHIP_FAMILY_NPCX7)
	case 0x06:
		*p++ = 'A';
		break;
	case 0x07:
		if (chip_id == 0x21 || chip_id == 0x24)
			*p++ = 'B';
		else
			*p++ = 'C';
		break;
#endif
	default:
		*p++ = system_to_hex((chip_gen & 0xF0) >> 4);
		*p++ = system_to_hex(chip_gen & 0x0F);
		break;
	}

	*p++ = '.';
	*p++ = system_to_hex((rev_num & 0xF0) >> 4);
	*p++ = system_to_hex(rev_num & 0x0F);
	*p++ = '\0';

	return rev;
}

BUILD_ASSERT(BBRM_DATA_INDEX_VBNVCNTXT + EC_VBNV_BLOCK_SIZE <= NPCX_BBRAM_SIZE);

/**
 * Set a scratchpad register to the specified value.
 *
 * The scratchpad register must maintain its contents across a
 * software-requested warm reset.
 *
 * @param value		Value to store.
 * @return EC_SUCCESS, or non-zero if error.
 */
int system_set_scratchpad(uint32_t value)
{
	return bbram_data_write(BBRM_DATA_INDEX_SCRATCHPAD, value);
}

uint32_t system_get_scratchpad(void)
{
	return bbram_data_read(BBRM_DATA_INDEX_SCRATCHPAD);
}

int system_is_reboot_warm(void)
{
	uint32_t reset_flags;

	/*
	 * Check reset cause here,
	 * gpio_pre_init is executed faster than system_pre_init
	 */
	check_reset_cause();
	reset_flags = system_get_reset_flags();

	if ((reset_flags & EC_RESET_FLAG_RESET_PIN) ||
	    (reset_flags & EC_RESET_FLAG_POWER_ON) ||
	    (reset_flags & EC_RESET_FLAG_WATCHDOG) ||
	    (reset_flags & EC_RESET_FLAG_HARD) ||
	    (reset_flags & EC_RESET_FLAG_SOFT) ||
	    (reset_flags & EC_RESET_FLAG_HIBERNATE))
		return 0;
	else
		return 1;
}

/*****************************************************************************/
/* Console commands */
void print_system_rtc(enum console_channel ch)
{
	uint32_t sec = system_get_rtc_sec();

	cprintf(ch, "RTC: 0x%08x (%d.00 s)\n", sec, sec);
}

#ifdef CONFIG_CMD_RTC
static int command_system_rtc(int argc, char **argv)
{
	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		char *e;
		uint32_t t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		system_set_rtc(t);
	} else if (argc > 1) {
		return EC_ERROR_INVAL;
	}

	print_system_rtc(CC_COMMAND);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc,
		"[set <seconds>]",
		"Get/set real-time clock");

#ifdef CONFIG_CMD_RTC_ALARM
/**
 * Test the RTC alarm by setting an interrupt on RTC match.
 */
static int command_rtc_alarm_test(int argc, char **argv)
{
	int s = 1, us = 0;
	char *e;

	ccprintf("Setting RTC alarm\n");
	system_enable_hib_interrupt();

	if (argc > 1) {
		s = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;

	}
	if (argc > 2) {
		us = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;

	}

	system_set_rtc_alarm(s, us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc_alarm, command_rtc_alarm_test,
		"[seconds [microseconds]]",
		"Test alarm");
#endif /* CONFIG_CMD_RTC_ALARM */
#endif /* CONFIG_CMD_RTC */

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_HOSTCMD_RTC
static enum ec_status system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_sec();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE,
		system_rtc_get_value,
		EC_VER_MASK(0));

static enum ec_status system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE,
		system_rtc_set_value,
		EC_VER_MASK(0));

static enum ec_status system_rtc_set_alarm(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc_alarm(p->time, 0);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_ALARM,
		system_rtc_set_alarm,
		EC_VER_MASK(0));

static enum ec_status system_rtc_get_alarm(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_alarm();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_ALARM,
		system_rtc_get_alarm,
		EC_VER_MASK(0));

#endif /* CONFIG_HOSTCMD_RTC */
#ifdef CONFIG_EXTERNAL_STORAGE
void system_jump_to_booter(void)
{
	enum API_RETURN_STATUS_T status __attribute__((unused));
	static uint32_t flash_offset;
	static uint32_t flash_used;
	static uint32_t addr_entry;

	/*
	 * Get memory offset and size for RO/RW regions.
	 * Both of them need 16-bytes alignment since GDMA burst mode.
	 */
	switch (system_get_shrspi_image_copy()) {
	case SYSTEM_IMAGE_RW:
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
		break;
#ifdef CONFIG_RW_B
	case SYSTEM_IMAGE_RW_B:
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_B_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
		break;
#endif
	case SYSTEM_IMAGE_RO:
	default: /* Jump to RO by default */
		flash_offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
				CONFIG_RO_STORAGE_OFF;
		flash_used = CONFIG_RO_SIZE;
		break;
	}

	/* Make sure the reset vector is inside the destination image */
	addr_entry = *(uintptr_t *)(flash_offset +
				    CONFIG_MAPPED_STORAGE_BASE + 4);

	/*
	 * Speed up FW download time by increasing clock freq of EC. It will
	 * restore to default in clock_init() later.
	 */
	clock_turbo();

	/* Bypass for GMDA issue of ROM api utilities */
#if defined(CHIP_FAMILY_NPCX5)
	system_download_from_flash(
		flash_offset,      /* The offset of the data in spi flash */
		CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of downloaded data */
		flash_used,        /* Number of bytes to download      */
		addr_entry         /* jump to this address after download */
	);
#else
	download_from_flash(
		flash_offset,      /* The offset of the data in spi flash */
		CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of downloaded data */
		flash_used,        /* Number of bytes to download      */
		SIGN_NO_CHECK,     /* Need CRC check or not               */
		addr_entry,        /* jump to this address after download */
		&status            /* Status fo download */
	);
#endif
}

uint32_t system_get_lfw_address()
{
	/*
	 * In A3 version, we don't use little FW anymore
	 * We provide the alternative function in ROM
	 */
	uint32_t jump_addr = (uint32_t)system_jump_to_booter;
	return jump_addr;
}

/*
 * Set and clear image copy flags in MDC register.
 *
 * NPCX_FWCTRL_RO_REGION: 1 - RO, 0 - RW
 * NPCX_FWCTRL_FW_SLOT: 1 - SLOT_A, 0 - SLOT_B
 */
void system_set_image_copy(enum system_image_copy_t copy)
{
	switch (copy) {
	case SYSTEM_IMAGE_RW:
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
#ifdef CONFIG_RW_B
	case SYSTEM_IMAGE_RW_B:
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
#endif
	default:
		CPRINTS("Invalid copy (%d) is requested as a jump destination. "
			"Change it to %d.", copy, SYSTEM_IMAGE_RO);
		/* Fall through to SYSTEM_IMAGE_RO */
	case SYSTEM_IMAGE_RO:
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
	}
}

enum system_image_copy_t system_get_shrspi_image_copy(void)
{
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION)) {
		/* RO image */
#ifdef CHIP_HAS_RO_B
		if (!IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT))
			return SYSTEM_IMAGE_RO_B;
#endif
		return SYSTEM_IMAGE_RO;
	} else {
#ifdef CONFIG_RW_B
		/* RW image */
		if (!IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT))
			/* Slot A */
			return SYSTEM_IMAGE_RW_B;
#endif
		return SYSTEM_IMAGE_RW;
	}
}

#endif
