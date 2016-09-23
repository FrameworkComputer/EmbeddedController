/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : NPCX hardware specific implementation */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "gpio.h"
#include "hwtimer_chip.h"
#include "system_chip.h"
#include "clock_chip.h"
#include "rom_chip.h"

/* Flags for BBRM_DATA_INDEX_WAKE */
#define HIBERNATE_WAKE_MTC        (1 << 0)  /* MTC alarm */
#define HIBERNATE_WAKE_PIN        (1 << 1)  /* Wake pin */

/* Delay after writing TTC for value to latch */
#define MTC_TTC_LOAD_DELAY_US 250
#define MTC_ALARM_MASK     ((1 << 25) - 1)
#define MTC_WUI_GROUP      MIWU_GROUP_4
#define MTC_WUI_MASK       MASK_PIN7

/* ROM address of chip revision */
#define CHIP_REV_ADDR 0x00007FFC

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Begin address of Suspend RAM for hibernate utility */
uintptr_t __lpram_fw_start = CONFIG_LPRAM_BASE;

/* Offset of little FW in Suspend Ram for GDMA bypass */
#define LFW_OFFSET 0x160
/* Begin address of Suspend RAM for little FW (GDMA utilities). */
uintptr_t __lpram_lfw_start = CONFIG_LPRAM_BASE + LFW_OFFSET;

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

/**
 * Read battery-backed ram (BBRAM) at specified index.
 *
 * @return The value of the register or 0 if invalid index.
 */
static uint32_t bbram_data_read(enum bbram_data_index index)
{
	uint32_t value = 0;
	/* Check index */
	if (index < 0 || index >= NPCX_BBRAM_SIZE)
		return 0;

	/* BBRAM is valid */
	if (IS_BIT_SET(NPCX_BKUP_STS, NPCX_BKUP_STS_IBBR))
		return 0;

	/* Read BBRAM */
	value += NPCX_BBRAM(index + 3);
	value = value << 8;
	value += NPCX_BBRAM(index + 2);
	value = value << 8;
	value += NPCX_BBRAM(index + 1);
	value = value << 8;
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
	/* Check index */
	if (index < 0 || index >= NPCX_BBRAM_SIZE)
		return EC_ERROR_INVAL;

	/* BBRAM is valid */
	if (IS_BIT_SET(NPCX_BKUP_STS, NPCX_BKUP_STS_IBBR))
		return EC_ERROR_INVAL;

	/* Write BBRAM */
	NPCX_BBRAM(index)     = value & 0xFF;
	NPCX_BBRAM(index + 1) = (value >> 8)  & 0xFF;
	NPCX_BBRAM(index + 2) = (value >> 16) & 0xFF;
	NPCX_BBRAM(index + 3) = (value >> 24) & 0xFF;

	/* Wait for write-complete */
	return EC_SUCCESS;
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

/* Check reset cause */
void system_check_reset_cause(void)
{
	uint32_t hib_wake_flags = bbram_data_read(BBRM_DATA_INDEX_WAKE);
	uint32_t flags = bbram_data_read(BBRM_DATA_INDEX_SAVED_RESET_FLAGS);

	/* Clear saved reset flags in bbram */
	bbram_data_write(BBRM_DATA_INDEX_SAVED_RESET_FLAGS, 0);
	/* Clear saved hibernate wake flag in bbram , too */
	bbram_data_write(BBRM_DATA_INDEX_WAKE, 0);

	/* Use scratch bit to check power on reset or VCC1_RST reset */
	if (!IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_SCRATCH)) {
#ifdef BOARD_WHEATLEY
		flags |= RESET_FLAG_RESET_PIN;
#else
		/* Check for VCC1 reset */
		if (IS_BIT_SET(NPCX_RSTCTL, NPCX_RSTCTL_VCC1_RST_STS))
			flags |= RESET_FLAG_RESET_PIN;
		else
			flags |= RESET_FLAG_POWER_ON;
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
		flags |= RESET_FLAG_SOFT;
		/* Clear debugger reset status initially*/
		SET_BIT(NPCX_RSTCTL, NPCX_RSTCTL_DBGRST_STS);
	}

	/* Reset by hibernate */
	if (hib_wake_flags & HIBERNATE_WAKE_PIN)
		flags |= RESET_FLAG_WAKE_PIN | RESET_FLAG_HIBERNATE;
	else if (hib_wake_flags & HIBERNATE_WAKE_MTC)
		flags |= RESET_FLAG_RTC_ALARM | RESET_FLAG_HIBERNATE;

	/* Watchdog Reset */
	if (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS)) {
		/*
		 * Don't set RESET_FLAG_WATCHDOG flag if watchdog is issued by
		 * system_reset or hibernate in order to distinguish reset cause
		 * is panic reason or not.
		 */
		if (!(flags & (RESET_FLAG_SOFT | RESET_FLAG_HARD |
				RESET_FLAG_HIBERNATE)))
			flags |= RESET_FLAG_WATCHDOG;

		/* Clear watchdog reset status initially*/
		SET_BIT(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS);
	}

	system_set_reset_flags(flags);
}

/**
 * Configure address 0x40001600 in the the MPU
 * (Memory Protection Unit) as a "regular" memory
 */
void system_mpu_config(void)
{
	/* Enable MPU */
	CPU_MPU_CTRL = 0x7;

	/* Create a new MPU Region for low-power ram */
	CPU_MPU_RNR  = 0;                         /* Select region number 0 */
	CPU_MPU_RASR = CPU_MPU_RASR & 0xFFFFFFFE; /* Disable region */
	CPU_MPU_RBAR = CONFIG_LPRAM_BASE;         /* Set region base address */
	/*
	 * Set region size & attribute and enable region
	 * [31:29] - Reserved.
	 * [28]    - XN (Execute Never) = 0
	 * [27]    - Reserved.
	 * [26:24] - AP                 = 011 (Full access)
	 * [23:22] - Reserved.
	 * [21:19,18,17,16] - TEX,S,C,B = 001000 (Normal memory)
	 * [15:8]  - SRD                = 0 (Subregions enabled)
	 * [7:6]   - Reserved.
	 * [5:1]   - SIZE               = 01001 (1K)
	 * [0]     - ENABLE             = 1 (enabled)
	 */
	CPU_MPU_RASR = 0x03080013;

	/* Create a new MPU Region for data ram */
	CPU_MPU_RNR  = 1;                         /* Select region number 1 */
	CPU_MPU_RASR = CPU_MPU_RASR & 0xFFFFFFFE; /* Disable region */
	CPU_MPU_RBAR = CONFIG_RAM_BASE;           /* Set region base address */
	/*
	 * Set region size & attribute and enable region
	 * [31:29] - Reserved.
	 * [28]    - XN (Execute Never) = 1
	 * [27]    - Reserved.
	 * [26:24] - AP                 = 011 (Full access)
	 * [23:22] - Reserved.
	 * [21:19,18,17,16] - TEX,S,C,B = 001000 (Normal memory)
	 * [15:8]  - SRD                = 0 (Subregions enabled)
	 * [7:6]   - Reserved.
	 * [5:1]   - SIZE               = 01110 (32K)
	 * [0]     - ENABLE             = 1 (enabled)
	 */
	CPU_MPU_RASR = 0x1308001D;
}

void __keep __attribute__ ((noreturn, section(".lowpower_ram")))
__enter_hibernate_in_lpram(void)
{
	/*
	 * TODO (ML): Set stack pointer to upper 512B of Suspend RAM.
	 * Our bypass needs stack instructions but FW will turn off main ram
	 * later for better power consumption.
	 */
	asm (
		"ldr r0, =0x40001800\n"
		"mov sp, r0\n"
	);

	/* Disable Code RAM first */
	SET_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5), NPCX_PWDWN_CTL5_MRFSH_DIS);
	SET_BIT(NPCX_DISIDL_CTL, NPCX_DISIDL_CTL_RAM_DID);

	/* Set deep idle mode*/
	NPCX_PMCSR = 0x6;

	/* Enter deep idle, wake-up by GPIOxx or RTC */
	/*
	 * TODO (ML): Although the probability is small, it still has chance
	 * to meet the same symptom that CPU's behavior is abnormal after
	 * wake-up from deep idle.
	 * Workaround: Apply the same bypass of idle but don't enable interrupt.
	 */
	asm (
		"push {r0-r5}\n"        /* Save needed registers */
		"ldr r0, =0x40001600\n" /* Set r0 to Suspend RAM addr */
		"wfi\n"                 /* Wait for int to enter idle */
		"ldm r0, {r0-r5}\n"     /* Add a delay after WFI */
		"pop {r0-r5}\n"         /* Restore regs before enabling ints */
		"isb\n"                 /* Flush the cpu pipeline */
	);

	/* RTC wake-up */
	if (IS_BIT_SET(NPCX_WTC, NPCX_WTC_PTO))
		/*
		 * Mark wake-up reason for hibernate
		 * Do not call bbram_data_write directly cause of
		 * executing in low-power ram
		 */
		NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_MTC;
	else
		/* Otherwise, we treat it as GPIOs wake-up */
		NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_PIN;

	/* Start a watchdog reset */
	NPCX_WDCNT = 0x01;
	/* Reload and restart Timer 0*/
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_RST);
	/* Wait for timer is loaded and restart */
	while (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_RST))
		;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

/**
 * Chip-level function to set GPIOs and wake-up inputs for hibernate.
 */
void system_set_gpios_and_wakeup_inputs_hibernate(void)
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

	/* Enable wake-up inputs of hibernate_wake_pins array */
	for (i = 0; i < hibernate_wake_pins_used; i++) {
		gpio_reset(hibernate_wake_pins[i]);
		/* Re-enable interrupt for wake-up inputs */
		gpio_enable_interrupt(hibernate_wake_pins[i]);
	}
}

/**
 * Internal hibernate function.
 *
 * @param seconds      Number of seconds to sleep before LCT alarm
 * @param microseconds Number of microseconds to sleep before LCT alarm
 */
void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
{
	int i;
	void (*__hibernate_in_lpram)(void) =
			(void(*)(void))(__lpram_fw_start | 0x01);

	/* Enable power for the Low Power RAM */
	CLEAR_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6), 6);

	/* Disable ADC */
	NPCX_ADCCNF = 0;
	usleep(1000);

	/* Set SPI pins to be in Tri-State */
	SET_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);

	/* Disable instant wake up mode for better power consumption */
	CLEAR_BIT(NPCX_ENIDL_CTL, NPCX_ENIDL_CTL_LP_WK_CTL);

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

	/* Enable Low Power RAM */
	NPCX_LPRAM_CTRL = 1;

	/* Initialize watchdog */
	NPCX_TWCFG = 0; /* Select T0IN clock as watchdog prescaler clock */
	SET_BIT(NPCX_TWCFG, NPCX_TWCFG_WDCT0I);
	NPCX_TWCP = 0x00; /* Keep prescaler ratio timer0 clock to 1:1 */
	NPCX_TWDT0 = 0x00; /* Set internal counter and prescaler */

	/* Copy the __enter_hibernate_in_lpram instructions to LPRAM */
	for (i = 0; i < &__flash_lpfw_end - &__flash_lpfw_start; i++)
		*((uint32_t *)__lpram_fw_start + i) =
				*(&__flash_lpfw_start + i);

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

	/* execute hibernate func in LPRAM */
	__hibernate_in_lpram();

}

static char system_to_hex(uint8_t x)
{
	if (x >= 0 && x <= 9)
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

	if (seconds == EC_RTC_ALARM_CLEAR) {
		CLEAR_BIT(NPCX_WTC, NPCX_WTC_WIE);
		SET_BIT(NPCX_WTC, NPCX_WTC_PTO);

		return;
	}

	/* Get current clock */
	cur_secs = NPCX_TTC;

	/* If alarm clock is not sequential or not in range */
	alarm_secs = cur_secs + seconds;
	alarm_secs = alarm_secs & MTC_ALARM_MASK;

	/* Reset alarm first */
	system_reset_rtc_alarm();

	/* Set alarm, use first 25 bits of clock value */
	NPCX_WTC = alarm_secs;

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

#if SUPPORT_HIB
	/* Add additional hibernate operations here */
	__enter_hibernate(seconds, microseconds);
#endif
}

void system_pre_init(void)
{
	/*
	 * Add additional initialization here
	 * EC should be initialized in Booter
	 */

	/* Power-down the modules we don't need */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_1) = 0xF9; /* Skip SDP_PD FIU_PD */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_2) = 0xFF;
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_3) = 0x0F; /* Skip GDMA */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_4) = 0xF4; /* Skip ITIM2/1_PD */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5) = 0xF8;
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6) = 0xF5; /* Skip ITIM5_PD */

	/* Power down the modules used internally */
	NPCX_INTERNAL_CTRL1 = 0x03;
	NPCX_INTERNAL_CTRL2 = 0x03;
	NPCX_INTERNAL_CTRL3 = 0x03;

	/* Enable low-power regulator */
	CLEAR_BIT(NPCX_LFCGCALCNT, NPCX_LFCGCALCNT_LPREG_CTL_EN);
	SET_BIT(NPCX_LFCGCALCNT, NPCX_LFCGCALCNT_LPREG_CTL_EN);

	/*
	 * Configure LPRAM in the MPU as a regular memory
	 * and DATA RAM to prevent code execution
	 */
	system_mpu_config();
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	/* Add in AP off flag into saved flags. */
	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	/* Save reset flag */
	if (flags & SYSTEM_RESET_HARD)
		save_flags |= RESET_FLAG_HARD;
	else
		save_flags |= RESET_FLAG_SOFT;

	/* Store flags to battery backed RAM. */
	bbram_data_write(BBRM_DATA_INDEX_SAVED_RESET_FLAGS, save_flags);

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
	case 0x12:
		return "NPCX585G";
	case 0x13:
		return "NPCX575G";
	case 0x16:
		return "NPCX586G";
	case 0x17:
		return "NPCX576G";
	default:
		*p       = system_to_hex((chip_id & 0xF0) >> 4);
		*(p + 1) = system_to_hex(chip_id & 0x0F);
		*(p + 2) = '\0';
		return str;
	}
}

const char *system_get_chip_revision(void)
{
	static char rev[5];
	/* Read ROM data for chip revision directly */
	uint8_t rev_num = *((uint8_t *)CHIP_REV_ADDR);

	*(rev) = 'A';
	*(rev + 1) = '.';
	*(rev + 2) = system_to_hex((rev_num & 0xF0) >> 4);
	*(rev + 3) = system_to_hex(rev_num & 0x0F);
	*(rev + 4) = '\0';

	return rev;
}

BUILD_ASSERT(BBRM_DATA_INDEX_VBNVCNTXT + EC_VBNV_BLOCK_SIZE <= NPCX_BBRAM_SIZE);

/**
 * Get/Set VbNvContext in non-volatile storage.  The block should be 16 bytes
 * long, which is the current size of VbNvContext block.
 *
 * @param block		Pointer to a buffer holding VbNvContext.
 * @return 0 on success, !0 on error.
 */
int system_get_vbnvcontext(uint8_t *block)
{
	int i;

	if (IS_BIT_SET(NPCX_BKUP_STS, NPCX_BKUP_STS_IBBR)) {
		memset(block, 0, EC_VBNV_BLOCK_SIZE);
		return EC_SUCCESS;
	}

	for (i = 0; i < EC_VBNV_BLOCK_SIZE; ++i)
		block[i] = NPCX_BBRAM(BBRM_DATA_INDEX_VBNVCNTXT + i);

	return EC_SUCCESS;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	int i;

	if (IS_BIT_SET(NPCX_BKUP_STS, NPCX_BKUP_STS_IBBR))
		return EC_ERROR_INVAL;

	for (i = 0; i < EC_VBNV_BLOCK_SIZE; i++)
		NPCX_BBRAM(BBRM_DATA_INDEX_VBNVCNTXT + i) = block[i];

	return EC_SUCCESS;
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
	system_check_reset_cause();
	reset_flags = system_get_reset_flags();

	if ((reset_flags & RESET_FLAG_RESET_PIN) ||
	    (reset_flags & RESET_FLAG_POWER_ON) ||
	    (reset_flags & RESET_FLAG_WATCHDOG) ||
	    (reset_flags & RESET_FLAG_HARD) ||
	    (reset_flags & RESET_FLAG_SOFT))
		return 0;
	else
		return 1;
}

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_RTC
void print_system_rtc(enum console_channel ch)
{
	uint32_t sec = system_get_rtc_sec();

	cprintf(ch, "RTC: 0x%08x (%d.00 s)\n", sec, sec);
}

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
static int system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_sec();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE,
		system_rtc_get_value,
		EC_VER_MASK(0));

static int system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE,
		system_rtc_set_value,
		EC_VER_MASK(0));

static int system_rtc_set_alarm(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc_alarm(p->time, 0);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_ALARM,
		system_rtc_set_alarm,
		EC_VER_MASK(0));

static int system_rtc_get_alarm(struct host_cmd_handler_args *args)
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
void __keep __attribute__ ((noreturn, section(".lowpower_ram2")))
__start_gdma(uint32_t exeAddr)
{
	/* Enable GDMA now */
	SET_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAEN);

	/* Start GDMA */
	SET_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_SOFTREQ);

	/* Wait for transfer to complete/fail */
	while (!IS_BIT_SET(NPCX_GDMA_CTL, NPCX_GDMA_CTL_TC) &&
			!IS_BIT_SET(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAERR))
		;

	/* Disable GDMA now */
	CLEAR_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAEN);

	/*
	 * Failure occurs during GMDA transaction. Let watchdog issue and
	 * boot from RO region again.
	 */
	if (IS_BIT_SET(NPCX_GDMA_CTL, NPCX_GDMA_CTL_GDMAERR))
		while (1)
			;

	/*
	 * Jump to the exeAddr address if needed. Setting bit 0 of address to
	 * indicate it's a thumb branch for cortex-m series CPU.
	 */
	((void (*)(void))(exeAddr | 0x01))();

	/* Should never get here */
	while (1)
		;
}

static void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
		uint32_t size, uint32_t exeAddr)
{
	int i;
	uint8_t chunkSize = 16; /* 4 data burst mode. ie.16 bytes */
	/*
	 * GDMA utility in Suspend RAM. Setting bit 0 of address to indicate
	 * it's a thumb branch for cortex-m series CPU.
	 */
	void (*__start_gdma_in_lpram)(uint32_t) =
			(void(*)(uint32_t))(__lpram_lfw_start | 0x01);

	/*
	 * Before enabling burst mode for better performance of GDMA, it's
	 * important to make sure srcAddr, dstAddr and size of transactions
	 * are 16 bytes aligned in case failure occurs.
	 */
	ASSERT((size % chunkSize) == 0 && (srcAddr % chunkSize) == 0 &&
			(dstAddr % chunkSize) == 0);

	/* Check valid address for jumpiing */
	ASSERT(exeAddr != 0x0);

	/* Enable power for the Low Power RAM */
	CLEAR_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_6), 6);

	/* Enable Low Power RAM */
	NPCX_LPRAM_CTRL = 1;

	/*
	 * Initialize GDMA for flash reading.
	 * [31:21] - Reserved.
	 * [20]    - GDMAERR   = 0  (Indicate GMDA transfer error)
	 * [19]    - Reserved.
	 * [18]    - TC        = 0  (Terminal Count. Indicate operation is end.)
	 * [17]    - Reserved.
	 * [16]    - SOFTREQ   = 0  (Don't trigger here)
	 * [15]    - DM        = 0  (Set normal demand mode)
	 * [14]    - Reserved.
	 * [13:12] - TWS.      = 10 (One double-word for every GDMA transaction)
	 * [11:10] - Reserved.
	 * [9]     - BME       = 1  (4-data ie.16 bytes - Burst mode enable)
	 * [8]     - SIEN      = 0  (Stop interrupt disable)
	 * [7]     - SAFIX     = 0  (Fixed source address)
	 * [6]     - Reserved.
	 * [5]     - SADIR     = 0  (Source address incremented)
	 * [4]     - DADIR     = 0  (Destination address incremented)
	 * [3:2]   - GDMAMS    = 00 (Software mode)
	 * [1]     - Reserved.
	 * [0]     - ENABLE    = 0  (Don't enable yet)
	 */
	NPCX_GDMA_CTL = 0x00002200;

	/* Set source base address */
	NPCX_GDMA_SRCB = CONFIG_MAPPED_STORAGE_BASE + srcAddr;

	/* Set destination base address */
	NPCX_GDMA_DSTB = dstAddr;

	/* Set number of transfers */
	NPCX_GDMA_TCNT = (size / chunkSize);

	/* Clear Transfer Complete event */
	SET_BIT(NPCX_GDMA_CTL, NPCX_GDMA_CTL_TC);

	/* Copy the __start_gdma_in_lpram instructions to LPRAM */
	for (i = 0; i < &__flash_lplfw_end - &__flash_lplfw_start; i++)
		*((uint32_t *)__lpram_lfw_start + i) =
				*(&__flash_lplfw_start + i);

	/* Start GDMA in Suspend RAM */
	__start_gdma_in_lpram(exeAddr);
}

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
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION)) {
		flash_offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
				CONFIG_RO_STORAGE_OFF;
		flash_used = CONFIG_RO_SIZE;
	} else {
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
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
#if defined(CHIP_VARIANT_NPCX5M5G) || defined(CHIP_VARIANT_NPCX5M6G)
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

void system_set_image_copy(enum system_image_copy_t copy)
{
	/* Jump to RW region -- clear flag */
	if (copy == SYSTEM_IMAGE_RW)
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
	else /* Jump to RO region -- set flag */
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
}

enum system_image_copy_t system_get_shrspi_image_copy(void)
{
	/* RO region FW */
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION))
		return SYSTEM_IMAGE_RO;
	else/* RW region FW */
		return SYSTEM_IMAGE_RW;
}
#endif
