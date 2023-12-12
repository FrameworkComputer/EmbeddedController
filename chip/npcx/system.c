/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : NPCX hardware specific implementation */

#include "builtin/assert.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer_chip.h"
#include "lct_chip.h"
#include "panic.h"
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
#define MTC_ALARM_MASK (BIT(25) - 1)
#define MTC_WUI_GROUP MIWU_GROUP_4
#define MTC_WUI_MASK MASK_PIN7

/* ROM address of chip revision */
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
#define CHIP_REV_ADDR 0x0000FFFC
#define CHIP_REV_STR_SIZE 12
#define PWDWN_8_RESERVED_SET_MASK 0x30
#else
#define CHIP_REV_ADDR 0x00007FFC
#define CHIP_REV_STR_SIZE 6
#endif

/*  Legacy SuperI/O Configuration D register offset */
#define SIOCFD_REG_OFFSET 0x2D

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#if defined(NPCX_LCT_SUPPORT)
/* A flag for waking up from hibernate mode by RTC overflow event */
static int is_rtc_overflow_event;
#endif

/*****************************************************************************/
/* Internal functions */

void system_watchdog_reset(void)
{
	/* Unlock & stop watchdog registers */
	watchdog_stop_and_unlock();

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
	return index == BBRM_DATA_INDEX_PD0 || index == BBRM_DATA_INDEX_PD1 ||
	       index == BBRM_DATA_INDEX_PD2 ||
	       index == BBRM_DATA_INDEX_PANIC_FLAGS;
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
		 * npcx7m6fb/npcx7m6fc/npcx7m7fc/npcx7m7wb/npcx7m7wc:
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
		NPCX_BBRAM(index + 1) = (value >> 8) & 0xFF;
		NPCX_BBRAM(index + 2) = (value >> 16) & 0xFF;
		NPCX_BBRAM(index + 3) = (value >> 24) & 0xFF;
	}

	/* Wait for write-complete */
	return EC_SUCCESS;
}

/* Map idx to a returned BBRM_DATA_INDEX_*, or return -1 on invalid idx */
static int bbram_idx_lookup(enum system_bbram_idx idx)
{
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
 *   36      |       CFSR
 *   40      |       HFSR
 *   44      |       BFAR
 *   48      |      LREG1
 *   52      |      LREG3
 *   56      |      LREG4
 *   60      |     reserved
 *
 * Above registers are chosen to be saved in case of panic because:
 * 1. CFSR, HFSR and BFAR seem to provide more information about the fault.
 * 2. LREG1, LREG3 and LREG4 store exception, reason and info in case of
 * software panic.
 */
#define BKUP_CFSR (BBRM_DATA_INDEX_PANIC_BKUP + 0)
#define BKUP_HFSR (BBRM_DATA_INDEX_PANIC_BKUP + 4)
#define BKUP_BFAR (BBRM_DATA_INDEX_PANIC_BKUP + 8)
#define BKUP_LREG1 (BBRM_DATA_INDEX_PANIC_BKUP + 12)
#define BKUP_LREG3 (BBRM_DATA_INDEX_PANIC_BKUP + 16)
#define BKUP_LREG4 (BBRM_DATA_INDEX_PANIC_BKUP + 20)

#define BKUP_PANIC_DATA_VALID BIT(0)

void chip_panic_data_backup(void)
{
	struct panic_data *d = panic_get_data();

	if (!d)
		return;

	bbram_data_write(BKUP_CFSR, d->cm.cfsr);
	bbram_data_write(BKUP_HFSR, d->cm.hfsr);
	bbram_data_write(BKUP_BFAR, d->cm.dfsr);
	bbram_data_write(BKUP_LREG1, d->cm.regs[1]);
	bbram_data_write(BKUP_LREG3, d->cm.regs[3]);
	bbram_data_write(BKUP_LREG4, d->cm.regs[4]);
	bbram_data_write(BBRM_DATA_INDEX_PANIC_FLAGS, BKUP_PANIC_DATA_VALID);
}

static void chip_panic_data_restore(void)
{
	struct panic_data *d;

	/* Ensure BBRAM is valid. */
	if (!bbram_valid(BKUP_CFSR, 4))
		return;

	/* Ensure Panic data in BBRAM is valid. */
	if (!(bbram_data_read(BBRM_DATA_INDEX_PANIC_FLAGS) &
	      BKUP_PANIC_DATA_VALID))
		return;

	d = get_panic_data_write();

	memset(d, 0, CONFIG_PANIC_DATA_SIZE);
	d->magic = PANIC_DATA_MAGIC;
	d->struct_size = CONFIG_PANIC_DATA_SIZE;
	d->struct_version = 2;
	d->arch = PANIC_ARCH_CORTEX_M;

	d->cm.cfsr = bbram_data_read(BKUP_CFSR);
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

static void chip_set_hib_flag(uint32_t *flags, uint32_t hib_wake_flags)
{
	/* Hibernate via PSL */
	if (hib_wake_flags & HIBERNATE_WAKE_PSL) {
#ifdef NPCX_LCT_SUPPORT
		if (npcx_lct_is_event_set()) {
			*flags |= EC_RESET_FLAG_RTC_ALARM |
				  EC_RESET_FLAG_HIBERNATE;
			/* Is RTC overflow event? */
			if (bbram_data_read(BBRM_DATA_INDEX_LCT_TIME) ==
			    NPCX_LCT_MAX) {
				/*
				 * Mark it as RTC overflow event and handle it
				 * in hook init function later for logging info.
				 */
				is_rtc_overflow_event = 1;
			}
			npcx_lct_clear_event();
			return;
		}
#endif
		*flags |= EC_RESET_FLAG_WAKE_PIN | EC_RESET_FLAG_HIBERNATE;
	} else { /* Hibernate via non-PSL */
#ifdef NPCX_LCT_SUPPORT
		if (hib_wake_flags & HIBERNATE_WAKE_LCT) {
			*flags |= EC_RESET_FLAG_RTC_ALARM |
				  EC_RESET_FLAG_HIBERNATE;
			return;
		}
#endif
		if (hib_wake_flags & HIBERNATE_WAKE_PIN) {
			*flags |= EC_RESET_FLAG_WAKE_PIN |
				  EC_RESET_FLAG_HIBERNATE;
		} else if (hib_wake_flags & HIBERNATE_WAKE_MTC) {
			*flags |= EC_RESET_FLAG_RTC_ALARM |
				  EC_RESET_FLAG_HIBERNATE;
		}
	}
}

static void check_reset_cause(void)
{
	uint32_t hib_wake_flags = bbram_data_read(BBRM_DATA_INDEX_WAKE);
	uint32_t chip_flags = chip_read_reset_flags();
	uint32_t flags = chip_flags;

	/* Clear saved reset flags in bbram */
#ifdef CONFIG_POWER_BUTTON_INIT_IDLE
	/*
	 * We're not sure whether we're booting or not. AP_IDLE will be cleared
	 * on S5->S3 transition.
	 */
	chip_flags &= EC_RESET_FLAG_AP_IDLE;
#else
	chip_flags = 0;
#endif
	/* Clear saved hibernate wake flag in bbram , too */
	bbram_data_write(BBRM_DATA_INDEX_WAKE, 0);

	chip_set_hib_flag(&flags, hib_wake_flags);

	/* Use scratch bit to check power on reset or VCC1_RST reset */
	if (!IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_SCRATCH)) {
#ifdef CONFIG_BOARD_FORCE_RESET_PIN
		/* Treat all resets as RESET_PIN */
		flags |= EC_RESET_FLAG_RESET_PIN;
#else
		/* Check for VCC1 reset */
		int reset = IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_STS);

		/*
		 * If configured, check the saved flags to see whether
		 * the previous restart was a power-on, in which case
		 * treat this restart as a power-on as well.
		 * This is to workaround the fact that the H1 will
		 * reset the EC at power up.
		 */
		if (IS_ENABLED(CONFIG_BOARD_RESET_AFTER_POWER_ON)) {
			/*
			 * Reset pin restart rather than power-on, so check
			 * for any flag set from a previous power-on.
			 */
			if (reset) {
				if (flags & EC_RESET_FLAG_INITIAL_PWR)
					/*
					 * The previous restart was a power-on
					 * so treat this restart as that, and
					 * clear the flag so later code will
					 * not wait for the second reset.
					 */
					flags = (flags &
						 ~EC_RESET_FLAG_INITIAL_PWR) |
						EC_RESET_FLAG_POWER_ON;
				else
					/*
					 * No previous power-on flag,
					 * so this is a subsequent restart
					 * i.e any restarts after the
					 * second restart caused by the H1.
					 */
					flags |= EC_RESET_FLAG_RESET_PIN;
			} else {
				flags |= EC_RESET_FLAG_POWER_ON;

				/*
				 * Power-on restart, so set a flag and save it
				 * for the next imminent reset. Later code
				 * will check for this flag and wait for the
				 * second reset. Waking from PSL hibernate is
				 * power-on for EC but not for H1, so do not
				 * wait for the second reset.
				 */
				if (!(flags & EC_RESET_FLAG_HIBERNATE)) {
					flags |= EC_RESET_FLAG_INITIAL_PWR;
					chip_flags |= EC_RESET_FLAG_INITIAL_PWR;
				}
			}
		} else
			/*
			 * No second reset after power-on, so
			 * set the flags according to the restart reason.
			 */
			flags |= reset ? EC_RESET_FLAG_RESET_PIN :
					 EC_RESET_FLAG_POWER_ON;
#endif
	}
	chip_save_reset_flags(chip_flags);

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
	for (table = MIWU_TABLE_0; table < MIWU_TABLE_2; table++) {
		for (i = 0; i < 8; i++) {
			/* Disable all wake-ups */
			NPCX_WKEN(table, i) = 0x00;
			/* Clear all pending bits of wake-ups */
			NPCX_WKPCL(table, i) = 0xFF;
			/*
			 * Disable all inputs of wake-ups to prevent leakage
			 * caused by input floating.
			 */
			NPCX_WKINEN(table, i) = 0x00;
		}
	}

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	/* Disable MIWU 2 group 6 inputs which used for the additional GPIOs */
	NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_6) = 0x00;
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

#ifdef NPCX_LCT_SUPPORT
static void system_set_lct_alarm(uint32_t seconds, uint32_t microseconds)
{
	/* The min resolution of LCT is 1 seconds */
	if ((seconds == 0) && (microseconds != 0))
		seconds = 1;

	npcx_lct_enable(0);
	npcx_lct_sel_power_src(NPCX_LCT_PWR_SRC_VSBY);
#ifdef CONFIG_HIBERNATE_PSL
	/* Enable LCT event to PSL */
	npcx_lct_config(seconds, 1, 0);
	/* save the start time of LCT */
	if (IS_ENABLED(CONFIG_HOSTCMD_RTC) || IS_ENABLED(CONFIG_CMD_RTC))
		bbram_data_write(BBRM_DATA_INDEX_LCT_TIME, seconds);
#else
	/* Enable LCT event interrupt and MIWU */
	npcx_lct_config(seconds, 0, 1);
	task_disable_irq(NPCX_IRQ_LCT_WKINTF_2);
	/* Enable wake-up input sources & clear pending bit */
	NPCX_WKPCL(MIWU_TABLE_2, LCT_WUI_GROUP) |= LCT_WUI_MASK;
	NPCX_WKINEN(MIWU_TABLE_2, LCT_WUI_GROUP) |= LCT_WUI_MASK;
	NPCX_WKEN(MIWU_TABLE_2, LCT_WUI_GROUP) |= LCT_WUI_MASK;
	task_enable_irq(NPCX_IRQ_LCT_WKINTF_2);
#endif
	npcx_lct_enable(1);
}
#endif

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

#ifdef NPCX_LCT_SUPPORT
	/*
	 * This function must be called before the ITIM (system tick)
	 * is disabled because it calls udelay inside this function
	 */
	npcx_lct_enable_clk(1);
#endif

	/* Set SPI pins to be in Tri-State */
	SET_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);

	/* Disable instant wake up mode for better power consumption */
	CLEAR_BIT(NPCX_ENIDL_CTL, NPCX_ENIDL_CTL_LP_WK_CTL);

	/* Disable interrupt */
	interrupt_disable();

	/* Unlock & stop watchdog */
	watchdog_stop_and_unlock();

	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);
	/* ITIM time module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_ITEN);
	/* ITIM watchdog warn module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN);

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
	 * Give the board a chance to do any late stage hibernation work.  This
	 * is likely going to configure GPIOs for hibernation.  On some boards,
	 * it's possible that this may not return at all.  On those boards,
	 * power to the EC is likely being turn off entirely.
	 */
	if (board_hibernate_late)
		board_hibernate_late();

	/* Clear all pending IRQ otherwise wfi will have no affect */
	for (i = NPCX_IRQ_0; i < NPCX_IRQ_COUNT; i++)
		task_clear_pending_irq(i);

		/* Set the timer interrupt for wake up.  */
#ifdef NPCX_LCT_SUPPORT
	if (seconds || microseconds) {
		system_set_lct_alarm(seconds, microseconds);
	} else if (IS_ENABLED(CONFIG_HIBERNATE_PSL_COMPENSATE_RTC)) {
		system_set_lct_alarm(NPCX_LCT_MAX, 0);
	}
#else
	if (seconds || microseconds)
		system_set_rtc_alarm(seconds, microseconds);
#endif

	/* execute hibernate func depend on chip series */
	__hibernate_npcx_series();
}

#ifdef CONFIG_HIBERNATE_PSL_COMPENSATE_RTC
#ifndef NPCX_LCT_SUPPORT
#error "Do not enable CONFIG_HIBERNATE_PSL_COMPENSATE_RTC if npcx ec doesn't \
support LCT!"
#endif
/*
 * The function uses the LCT counter value to compensate for RTC after hibernate
 * wake-up. Because system_set_rtc() will invoke udelay(), the function should
 * execute after timer_init(). The function also should execute before
 * npcx_lct_init() which will clear all LCT register.
 */
void system_compensate_rtc(void)
{
	uint32_t rtc_time, ltc_start_time;

	ltc_start_time = bbram_data_read(BBRM_DATA_INDEX_LCT_TIME);
	if (ltc_start_time == 0)
		return;

	rtc_time = system_get_rtc_sec();
	rtc_time += ltc_start_time - npcx_lct_get_time();
	system_set_rtc(rtc_time);
	/* Clear BBRAM data to avoid compensating again. */
	bbram_data_write(BBRM_DATA_INDEX_LCT_TIME, 0);
}
#endif
#endif /* CONFIG_SUPPORT_CHIP_HIBERNATION */

static char system_to_hex(uint8_t val)
{
	uint8_t x = val & 0x0F;

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
	task_enable_irq(NPCX_IRQ_MTC);

	/* Enable wake-up input sources & clear pending bit */
	NPCX_WKPCL(MIWU_TABLE_0, MTC_WUI_GROUP) |= MTC_WUI_MASK;
	NPCX_WKINEN(MIWU_TABLE_0, MTC_WUI_GROUP) |= MTC_WUI_MASK;
	NPCX_WKEN(MIWU_TABLE_0, MTC_WUI_GROUP) |= MTC_WUI_MASK;
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
	task_disable_irq(NPCX_IRQ_MTC);
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
	task_enable_irq(NPCX_IRQ_MTC);
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

#ifndef CONFIG_ENABLE_JTAG_SELECTION
static void system_disable_host_sel_jtag(void)
{
	int data;

	/* Enable Core-to-Host Modules Access */
	SET_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSAE);
	/* Clear SIOCFD.JEN0_HSL to disable JTAG0 */
	data = sib_read_reg(SIO_OFFSET, SIOCFD_REG_OFFSET);
	data &= ~0x80;
	sib_write_reg(SIO_OFFSET, SIOCFD_REG_OFFSET, data);
	/* Disable Core-to-Host Modules Access */
	CLEAR_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSAE);
}
#endif

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
#if NPCX_FAMILY_VERSION < NPCX_FAMILY_NPCX9
	if (!IS_BIT_SET(NPCX_DEVALT(5), NPCX_DEVALT5_NJEN0_EN)) {
		/* Set DEVALT5.nJEN0_EN to disable JTAG0 */
		SET_BIT(NPCX_DEVALT(5), NPCX_DEVALT5_NJEN0_EN);
		system_disable_host_sel_jtag();
	}
#else
	if (GET_FIELD(NPCX_JEN_CTL1, NPCX_JEN_CTL1_JEN_EN_FIELD) ==
	    NPCX_JEN_CTL1_JEN_EN_ENA) {
		SET_FIELD(NPCX_JEN_CTL1, NPCX_JEN_CTL1_JEN_EN_FIELD,
			  NPCX_JEN_CTL1_JEN_EN_DIS);
		system_disable_host_sel_jtag();
	}
#endif
#endif
}

void system_pre_init(void)
{
	uint8_t pwdwn6;

	/*
	 * Add additional initialization here
	 * EC should be initialized in Booter
	 */

	/* Power down KBS, SDP, PS2, UART1, and MFT1-3 */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_1) = 0xFB;
	/* Power down PWM0-7 */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_2) = 0xFF;
#if defined(CHIP_FAMILY_NPCX5)
	/* Power down SMB0-3 */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_3) = 0x0F;
#elif NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	/* Power down SMB0-4 */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_3) = 0x3F;
#endif
	/* Power down ITIM3, ADC, PECI, SPIP */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_4) = 0xF4;
	/* Power down C2HACC, SHM_REG, SHM, Port80, and MSWC */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5) = 0xF8;

	pwdwn6 = 0x70 |
#if NPCX_FAMILY_VERSION <= NPCX_FAMILY_NPCX7
		 /*
		  * Don't set PD of ITIM6 for NPCX9 and later chips because
		  * they use it as the system timer.
		  */
		 BIT(NPCX_PWDWN_CTL6_ITIM6_PD) |
#endif
		 BIT(NPCX_PWDWN_CTL6_ITIM4_PD);
#if !defined(CONFIG_HOST_INTERFACE_ESPI)
	pwdwn6 |= 1 << NPCX_PWDWN_CTL6_ESPI_PD;
#endif
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6) = pwdwn6;

#if defined(CHIP_FAMILY_NPCX7)
#if defined(CHIP_VARIANT_NPCX7M6FB) || defined(CHIP_VARIANT_NPCX7M6FC) ||     \
	defined(CHIP_VARIANT_NPCX7M7FC) || defined(CHIP_VARIANT_NPCX7M7WB) || \
	defined(CHIP_VARIANT_NPCX7M7WC)
	/* Power down UART2, SMB5-7, ITIM64, and WoV */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_7) = 0xE7;
#else
	/* Power down SMB5-7 */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_7) = 0x07;
#endif
#endif
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	/* Power down UART2-4, SMB5-7, and ITIM64 */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_7) = 0xFF;
	/* Power down I3C */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_8) = PWDWN_8_RESERVED_SET_MASK | 0x01;
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

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	if (IS_ENABLED(CONFIG_HIBERNATE_PSL)) {
		uint8_t opt_flag = CONFIG_HIBERNATE_PSL_OUT_FLAGS;

		/* PSL Glitch Protection */
		SET_BIT(NPCX_GLUE_PSL_MCTL2, NPCX_GLUE_PSL_MCTL2_PSL_GP_EN);

		/*
		 * TODO: Remove this when NPCX9 A2 chip is available because A2
		 * chip will enable VCC1_RST to PSL wakeup source and lock it in
		 * the booter.
		 */
		if (IS_ENABLED(CONFIG_HIBERNATE_PSL_VCC1_RST_WAKEUP)) {
			/*
			 * Enable VCC1_RST as the wake-up source from
			 * hibernation.
			 */
			SET_BIT(NPCX_GLUE_PSL_MCTL1,
				NPCX_GLUE_PSL_MCTL1_VCC1_RST_PSL);
			/* Disable VCC_RST Pull-Up */
			SET_BIT(NPCX_DEVALT(ALT_GROUP_G),
				NPCX_DEVALTG_VCC1_RST_PUD);
			/*
			 * Lock this bit itself and VCC1_RST_PSL in the
			 * PSL_MCTL1 register to read-only.
			 */
			SET_BIT(NPCX_GLUE_PSL_MCTL2,
				NPCX_GLUE_PSL_MCTL2_VCC1_RST_PSL_LK);
		}

		/* Don't set PSL_OUT to open-drain if it is the level mode */
		ASSERT((opt_flag & NPCX_PSL_CFG_PSL_OUT_PULSE) ||
		       !(opt_flag & NPCX_PSL_CFG_PSL_OUT_OD));

		if (opt_flag & NPCX_PSL_CFG_PSL_OUT_OD)
			SET_BIT(NPCX_GLUE_PSL_MCTL1, NPCX_GLUE_PSL_MCTL1_OD_EN);
		else
			CLEAR_BIT(NPCX_GLUE_PSL_MCTL1,
				  NPCX_GLUE_PSL_MCTL1_OD_EN);

		if (opt_flag & NPCX_PSL_CFG_PSL_OUT_PULSE)
			SET_BIT(NPCX_GLUE_PSL_MCTL1,
				NPCX_GLUE_PSL_MCTL1_PLS_EN);
		else
			CLEAR_BIT(NPCX_GLUE_PSL_MCTL1,
				  NPCX_GLUE_PSL_MCTL1_PLS_EN);
	}
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
		*p = system_to_hex(fam_id >> 4);
		*(p + 1) = system_to_hex(fam_id);
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
	case NPCX585G_CHIP_ID:
		return "NPCX585G";
	case NPCX575G_CHIP_ID:
		return "NPCX575G";
	case NPCX586G_CHIP_ID:
		return "NPCX586G";
	case NPCX576G_CHIP_ID:
		return "NPCX576G";
#elif defined(CHIP_FAMILY_NPCX7)
	case NPCX787G_CHIP_ID:
		return "NPCX787G";
	case NPCX797F_C_CHIP_ID:
		return "NPCX797F";
	case NPCX796F_A_B_CHIP_ID:
	case NPCX796F_C_CHIP_ID:
		return "NPCX796F";
	case NPCX797W_B_CHIP_ID:
	case NPCX797W_C_CHIP_ID:
		return "NPCX797W";
#elif defined(CHIP_FAMILY_NPCX9)
	case NPCX996F_CHIP_ID:
		return "NPCX996F";
	case NPCX993F_CHIP_ID:
		return "NPCX993F";
	case NPCX99FP_CHIP_ID:
		return "NPCX99FP";
#endif
	default:
		*p = system_to_hex(chip_id >> 4);
		*(p + 1) = system_to_hex(chip_id);
		*(p + 2) = '\0';
		return str;
	}
}

const char *system_get_chip_revision(void)
{
	static char rev[CHIP_REV_STR_SIZE];
	char *p = rev;
	/* Read chip generation from SRID_CR */
	uint8_t chip_gen = NPCX_SRID_CR;
	/* Read ROM data for chip revision directly */
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	uint32_t rev_num = *((uint32_t *)CHIP_REV_ADDR);
#else
	uint8_t rev_num = *((uint8_t *)CHIP_REV_ADDR);
#endif

#ifdef CHIP_FAMILY_NPCX7
	uint8_t chip_id = NPCX_DEVICE_ID_CR;
#endif
	int s;

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
		if (chip_id == NPCX796F_A_B_CHIP_ID ||
		    chip_id == NPCX797W_B_CHIP_ID)
			*p++ = 'B';
		else
			*p++ = 'C';
		break;
#elif defined(CHIP_FAMILY_NPCX9)
	case 0x09:
		*p++ = 'A';
		break;
#endif
	default:
		*p++ = system_to_hex(chip_gen >> 4);
		*p++ = system_to_hex(chip_gen);
		break;
	}

	*p++ = '.';
	/*
	 * For npcx5/npcx7, the revision number is 1 byte.
	 * For NPCX9 and later chips, the revision number is 4 bytes.
	 */
	for (s = sizeof(rev_num) - 1; s >= 0; s--) {
		uint8_t r = rev_num >> (s * 8);

		*p++ = system_to_hex(r >> 4);
		*p++ = system_to_hex(r);
	}
	*p++ = '\0';

	return rev;
}

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

int system_get_scratchpad(uint32_t *value)
{
	*value = bbram_data_read(BBRM_DATA_INDEX_SCRATCHPAD);
	return EC_SUCCESS;
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

#if defined(CONFIG_HIBERNATE_PSL) && defined(NPCX_LCT_SUPPORT)
static void system_init_check_rtc_wakeup_event(void)
{
	/*
	 * If platform uses PSL (Power Switch Logic) for hibernating and RTC is
	 * also supported, determine whether ec is woken up by RTC with overflow
	 * event (16 weeks). If so, let it go to hibernate mode immediately.
	 */
	if (is_rtc_overflow_event) {
		CPRINTS("Hibernate due to RTC overflow event");
		system_hibernate(0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, system_init_check_rtc_wakeup_event,
	     HOOK_PRIO_DEFAULT - 1);
#endif

/*****************************************************************************/
/* Console commands */
void print_system_rtc(enum console_channel ch)
{
	uint32_t sec = system_get_rtc_sec();

	cprintf(ch, "RTC: 0x%08x (%d.00 s)\n", sec, sec);
}

#ifdef CONFIG_CMD_RTC
static int command_system_rtc(int argc, const char **argv)
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
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc, "[set <seconds>]",
			"Get/set real-time clock");

#ifdef CONFIG_CMD_RTC_ALARM
/**
 * Test the RTC alarm by setting an interrupt on RTC match.
 */
static int command_rtc_alarm_test(int argc, const char **argv)
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
			"[seconds [microseconds]]", "Test alarm");
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
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE, system_rtc_get_value,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE, system_rtc_set_value,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_set_alarm(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc_alarm(p->time, 0);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_ALARM, system_rtc_set_alarm,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_get_alarm(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_alarm();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_ALARM, system_rtc_get_alarm,
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
	case EC_IMAGE_RW:
		flash_offset =
			CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
		break;
#ifdef CONFIG_RW_B
	case EC_IMAGE_RW_B:
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
			       CONFIG_RW_B_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
		break;
#endif
	case EC_IMAGE_RO:
	default: /* Jump to RO by default */
		flash_offset =
			CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_RO_STORAGE_OFF;
		flash_used = CONFIG_RO_SIZE;
		break;
	}

	/* Make sure the reset vector is inside the destination image */
	addr_entry =
		*(uintptr_t *)(flash_offset + CONFIG_MAPPED_STORAGE_BASE + 4);

	/*
	 * Speed up FW download time by increasing clock freq of EC. It will
	 * restore to default in clock_init() later.
	 */
	clock_turbo();

	/* Bypass for GMDA issue of ROM api utilities */
#if defined(CHIP_FAMILY_NPCX5)
	system_download_from_flash(flash_offset, /* The offset of the data in
						    spi flash */
				   CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of
								  downloaded
								  data */
				   flash_used, /* Number of bytes to download */
				   addr_entry /* jump to this address after
						 download */
	);
#else
	download_from_flash(flash_offset, /* The offset of the data in spi flash
					   */
			    CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of
							   downloaded data */
			    flash_used, /* Number of bytes to download      */
			    SIGN_NO_CHECK, /* Need CRC check or not */
			    addr_entry, /* jump to this address after download
					 */
			    &status /* Status fo download */
	);
#endif
}

uint32_t system_get_lfw_address(void)
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
void system_set_image_copy(enum ec_image copy)
{
	switch (copy) {
	case EC_IMAGE_RW:
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
#ifdef CONFIG_RW_B
	case EC_IMAGE_RW_B:
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
#endif
	default:
		CPRINTS("Invalid copy (%d) is requested as a jump destination. "
			"Change it to %d.",
			copy, EC_IMAGE_RO);
		/* Fall through to EC_IMAGE_RO */
		__fallthrough;
	case EC_IMAGE_RO:
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
	}
}

enum ec_image system_get_shrspi_image_copy(void)
{
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION)) {
		/* RO image */
#ifdef CHIP_HAS_RO_B
		if (!IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT))
			return EC_IMAGE_RO_B;
#endif
		return EC_IMAGE_RO;
	} else {
#ifdef CONFIG_RW_B
		/* RW image */
		if (!IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT))
			/* Slot A */
			return EC_IMAGE_RW_B;
#endif
		return EC_IMAGE_RW;
	}
}

#endif
