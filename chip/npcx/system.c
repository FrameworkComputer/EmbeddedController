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
#include "rom_chip.h"

/* Flags for BBRM_DATA_INDEX_WAKE */
#define HIBERNATE_WAKE_MTC        (1 << 0)  /* MTC alarm */
#define HIBERNATE_WAKE_PIN        (1 << 1)  /* Wake pin */

/* equivalent to 250us according to 48MHz core clock */
#define MTC_TTC_LOAD_DELAY 1500
#define MTC_ALARM_MASK     ((1 << 25) - 1)
#define MTC_WUI_GROUP      MIWU_GROUP_4
#define MTC_WUI_MASK       MASK_PIN7

/* ROM address of chip revision */
#define CHIP_REV_ADDR 0x00007FFC

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Begin address for the .lpram section; defined in linker script */
uintptr_t __lpram_fw_start = CONFIG_LPRAM_BASE;

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
	volatile uint16_t __i;

	/* Set MTC counter unit:seconds */
	NPCX_TTC = seconds;

	/* Wait till clock is readable */
	for (__i = 0; __i < MTC_TTC_LOAD_DELAY; ++__i)
		;
}

/* Check reset cause */
void system_check_reset_cause(void)
{
	uint32_t hib_wake_flags = bbram_data_read(BBRM_DATA_INDEX_WAKE);
	uint32_t flags = 0;

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

	/* Watchdog Reset */
	if (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS)) {
		flags |= RESET_FLAG_WATCHDOG;
		/* Clear watchdog reset status initially*/
		SET_BIT(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS);
	}

	if ((hib_wake_flags & HIBERNATE_WAKE_PIN))
		flags |= RESET_FLAG_WAKE_PIN;
	else if ((hib_wake_flags & HIBERNATE_WAKE_MTC))
		flags |= RESET_FLAG_RTC_ALARM;

	/* Restore then clear saved reset flags */
	flags |= bbram_data_read(BBRM_DATA_INDEX_SAVED_RESET_FLAGS);
	bbram_data_write(BBRM_DATA_INDEX_SAVED_RESET_FLAGS, 0);
	/* Clear saved hibernate wake flag, too */
	bbram_data_write(BBRM_DATA_INDEX_WAKE, 0);

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

void __keep __attribute__ ((section(".lowpower_ram")))
__enter_hibernate_in_lpram(void)
{

	/* Disable Code RAM first */
	SET_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5), NPCX_PWDWN_CTL5_MRFSH_DIS);
	SET_BIT(NPCX_DISIDL_CTL, NPCX_DISIDL_CTL_RAM_DID);

	/* Set deep idle mode*/
	NPCX_PMCSR = 0x6;
	/* Enter deep idle, wake-up by GPIOxx or RTC */
	asm("wfi");

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

	/*
	 * Set all KBSOUTs to GPIOs and switch their mode to input and pull-up.
	 * Otherwise pressing the keyboard matrix might cause some current
	 * leakage during hibernating.
	 */
	NPCX_DEVALT(0x8) = 0xFF;
	NPCX_DEVALT(0x9) |= 0x1F;
	gpio_set_flags_by_mask(0x2, 0x03, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x1, 0xFF, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x0, 0xF0, GPIO_INPUT | GPIO_PULL_UP);

	/* Enable wake-up inputs of hibernate_wake_pins array */
	for (i = 0; i < hibernate_wake_pins_used; i++)
		gpio_reset(hibernate_wake_pins[i]);

#ifdef CONFIG_USB_PD_PORT_COUNT
	/*
	 * Leave USB-C charging enabled in hibernate, in order to
	 * allow wake-on-plug. 5V enable must be pulled low.
	 */
#if CONFIG_USB_PD_PORT_COUNT > 0
	gpio_set_flags(GPIO_USB_C0_5V_EN, GPIO_PULL_DOWN | GPIO_INPUT);
	gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 0);
#endif
#if CONFIG_USB_PD_PORT_COUNT > 1
	gpio_set_flags(GPIO_USB_C1_5V_EN, GPIO_PULL_DOWN | GPIO_INPUT);
	gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 0);
#endif
#endif /* CONFIG_USB_PD_PORT_COUNT */

	/* board-level function to set GPIOs state in hibernate */
	if (board_set_gpio_hibernate_state)
		board_set_gpio_hibernate_state();
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

/* Microsecond will be ignore for hardware limitation */
void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds)
{
	uint32_t cur_secs, alarm_secs;

	if (seconds == 0)
		return;

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

/**
 * Enable hibernate interrupt
 */
void system_enable_hib_interrupt(void)
{
	task_enable_irq(NPCX_IRQ_MTC_WKINTAD_0);
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	if (board_hibernate)
		board_hibernate();

	/* Flush console before hibernating */
	cflush();

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
	uint32_t *pblock = (uint32_t *) block;
	for (i = 0; i < 4; i++)
		pblock[i] = bbram_data_read(BBRM_DATA_INDEX_VBNVCNTXT + i*4);

	return EC_SUCCESS;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	int i, result;
	uint32_t *pblock = (uint32_t *) block;
	for (i = 0; i < 4; i++) {
		result = bbram_data_write(BBRM_DATA_INDEX_VBNVCNTXT + i*4,
				pblock[i]);
		if (result != EC_SUCCESS)
			return result;
	}
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

static int command_system_rtc(int argc, char **argv)
{
	uint32_t sec;
	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		char *e;
		uint32_t t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		system_set_rtc(t);
	} else if (argc > 1) {
		return EC_ERROR_INVAL;
	}

	sec = system_get_rtc_sec();
	ccprintf("RTC: 0x%08x (%d.00 s)\n", sec, sec);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc,
		"[set <seconds>]",
		"Get/set real-time clock",
		NULL);

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
		"Test alarm",
		NULL);
#endif /* CONFIG_CMD_RTC_ALARM */

/*****************************************************************************/
/* Host commands */

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

#ifdef CONFIG_EXTERNAL_STORAGE
void system_jump_to_booter(void)
{
	enum API_RETURN_STATUS_T status;
	static uint32_t flash_offset;
	static uint32_t flash_used;
	static uint32_t addr_entry;

	/* RO region FW */
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION)) {
		flash_offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
				CONFIG_RO_STORAGE_OFF;
		flash_used = CONFIG_RO_SIZE;
	} else { /* RW region FW */
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
	}

	/* Make sure the reset vector is inside the destination image */
	addr_entry = *(uintptr_t *)(flash_offset +
				    CONFIG_MAPPED_STORAGE_BASE + 4);

	download_from_flash(
		flash_offset,      /* The offset of the data in spi flash */
		CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of downloaded data */
		flash_used,        /* Number of bytes to download      */
		SIGN_NO_CHECK,     /* Need CRC check or not               */
		addr_entry,        /* jump to this address after download */
		&status            /* Status fo download */
	);
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
