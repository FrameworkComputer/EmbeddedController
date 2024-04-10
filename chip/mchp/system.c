/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : MCHP hardware specific implementation */

#include "clock.h"
#include "clock_chip.h"
#include "common.h" /* includes config.h and board.h */
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc_chip.h"
#include "registers.h"
#include "shared_mem.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)

/* Index values for hibernate data registers (RAM backed by VBAT) */
enum hibdata_index {
	HIBDATA_INDEX_SCRATCHPAD = 0, /* General-purpose scratch pad */
	HIBDATA_INDEX_SAVED_RESET_FLAGS, /* Saved reset flags */
	HIBDATA_INDEX_PD0, /* USB-PD0 saved port state */
	HIBDATA_INDEX_PD1, /* USB-PD1 saved port state */
	HIBDATA_INDEX_PD2, /* USB-PD2 saved port state */
};

/*
 * Voltage rail configuration
 * MEC172x VTR1 is 3.3V only, VTR2 is auto-detected 3.3 or 1.8V, and
 * VTR3 is always 1.8V.
 * MEC170x and MEC152x require manual selection of VTR3 for 1.8 or 3.3V.
 * The eSPI pins are on VTR3 and require 1.8V
 */
#ifdef CHIP_FAMILY_MEC172X
static void vtr3_voltage_select(int use18v)
{
	(void)use18v;
}
#else
static void vtr3_voltage_select(int use18v)
{
	if (use18v)
		MCHP_EC_GPIO_BANK_PWR |= MCHP_EC_GPIO_BANK_PWR_VTR3_18;
	else
		MCHP_EC_GPIO_BANK_PWR &= ~(MCHP_EC_GPIO_BANK_PWR_VTR3_18);
}
#endif

/*
 * The current logic will set EC_RESET_FLAG_RESET_PIN flag
 * even if the reset was caused by WDT. MEC170x/MEC152x HW RESET_SYS
 * status goes active for any of the following:
 *	RESET_VTR: power rail change
 *	WDT Event: WDT timed out
 *	FW triggered chip reset: SYSRESETREQ or PCR sys reset bit
 * The code does check WDT status in the VBAT PFR register.
 * Is it correct to report both EC_RESET_FLAG_RESET_PIN and
 * EC_RESET_FLAG_WATCHDOG on a WDT only reset?
 */
static void check_reset_cause(void)
{
	uint32_t status = MCHP_VBAT_STS;
	uint32_t flags = 0;
	uint32_t rst_sts = MCHP_PCR_PWR_RST_STS &
			   (MCHP_PWR_RST_STS_SYS | MCHP_PWR_RST_STS_VBAT);

	/* Clear the reset causes now that we've read them */
	MCHP_VBAT_STS |= status;
	MCHP_PCR_PWR_RST_STS |= rst_sts;

	/*
	 * BIT[6] indicates RESET_SYS asserted.
	 * RESET_SYS will assert on VTR reset, WDT reset, or
	 * firmware triggering a reset using Cortex-M4 SYSRESETREQ
	 * or MCHP PCR system reset register.
	 */
	if (rst_sts & MCHP_PWR_RST_STS_SYS)
		flags |= EC_RESET_FLAG_RESET_PIN;

	flags |= chip_read_reset_flags();
	chip_save_reset_flags(0);

	if ((status & MCHP_VBAT_STS_WDT) &&
	    !(flags & (EC_RESET_FLAG_SOFT | EC_RESET_FLAG_HARD |
		       EC_RESET_FLAG_HIBERNATE)))
		flags |= EC_RESET_FLAG_WATCHDOG;

	system_set_reset_flags(flags);
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
	    (reset_flags & EC_RESET_FLAG_SOFT))
		return 0;
	else
		return 1;
}

/*
 * Sleep unused blocks to reduce power.
 * Drivers/modules will clear PCR sleep enables for their blocks.
 * Keep sleep enables cleared for required blocks:
 * ECIA, PMC, CPU, ECS and optionally JTAG.
 * SLEEP_ALL feature will set these upon sleep entry.
 * Based on CONFIG_CHIPSET_DEBUG enable or disable ARM SWD
 * 2-pin JTAG mode.
 */
static void chip_periph_sleep_control(void)
{
	uint32_t d;

	d = MCHP_PCR_SLP_EN0_SLEEP;

	if (IS_ENABLED(CONFIG_CHIPSET_DEBUG)) {
		d &= ~(MCHP_PCR_SLP_EN0_JTAG);
		MCHP_EC_JTAG_EN = MCHP_JTAG_MODE_SWD | MCHP_JTAG_ENABLE;
	} else
		MCHP_EC_JTAG_EN &= ~(MCHP_JTAG_ENABLE);

	MCHP_PCR_SLP_EN0 = d;
	MCHP_PCR_SLP_EN1 = MCHP_PCR_SLP_EN1_UNUSED_BLOCKS;
	MCHP_PCR_SLP_EN2 = MCHP_PCR_SLP_EN2_SLEEP;
	MCHP_PCR_SLP_EN3 = MCHP_PCR_SLP_EN3_SLEEP;
	MCHP_PCR_SLP_EN4 = MCHP_PCR_SLP_EN4_SLEEP;
}

#ifdef CONFIG_CHIP_PRE_INIT
void chip_pre_init(void)
{
	chip_periph_sleep_control();

	if (IS_ENABLED(CONFIG_MCHP_TFDP)) {
		/* MCHP Enable TFDP for fast debug messages */
		tfdp_power(1);
		tfdp_enable(1, 1);
		CPRINTS("chip_pre_init: Image type = 0x%02x",
			MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX));
	}
}
#endif

void system_pre_init(void)
{
	/*
	 * Make sure AHB Error capture is enabled.
	 * Signals bus fault to Cortex-M4 core if an address presented
	 * to AHB is not claimed by any HW block.
	 */
	MCHP_EC_AHB_ERR = 0; /* write any value to clear */
	MCHP_EC_AHB_ERR_EN = 0; /* enable capture of address on error */

	/* Manual voltage selection only required for MEC170x and MEC152x */
	if (IS_ENABLED(CONFIG_HOST_INTERFACE_ESPI))
		vtr3_voltage_select(1);
	else
		vtr3_voltage_select(0);

	if (!IS_ENABLED(CONFIG_CHIP_PRE_INIT))
		chip_periph_sleep_control();

	/* Enable direct NVIC */
	MCHP_EC_INT_CTRL |= 1;

	/* Disable ARM TRACE debug port */
	MCHP_EC_TRACE_EN &= ~1;

	/*
	 * Enable aggregated only interrupt GIRQ's
	 * Make sure direct mode interrupt sources aggregated outputs
	 * are not enabled.
	 * Aggregated only GIRQ's 8,9,10,11,12,22,24,25,26
	 * Direct GIRQ's = 13,14,15,16,17,18,19,21,23
	 * These bits only need to be touched again on RESET_SYS.
	 * NOTE: GIRQ22 wake for AHB peripherals not processor.
	 */
	MCHP_INT_BLK_DIS = 0xfffffffful;
	MCHP_INT_BLK_EN = MCHP_INT_AGGR_ONLY_BITMAP;

	spi_enable(SPI_FLASH_DEVICE, 1);
}

uint32_t chip_read_reset_flags(void)
{
	return MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS);
}

void chip_save_reset_flags(uint32_t flags)
{
	MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS) = flags;
}

__noreturn void _system_reset(int flags, int wake_from_hibernate)
{
	uint32_t save_flags = 0;

	/* DEBUG */
	CPRINTS("MEC system reset: flag = 0x%08x wake = %d", flags,
		wake_from_hibernate);

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | EC_RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= EC_RESET_FLAG_AP_OFF;

	if (wake_from_hibernate)
		save_flags |= EC_RESET_FLAG_HIBERNATE;
	else if (flags & SYSTEM_RESET_HARD)
		save_flags |= EC_RESET_FLAG_HARD;
	else
		save_flags |= EC_RESET_FLAG_SOFT;

	chip_save_reset_flags(save_flags);

	/*
	 * Trigger chip reset
	 */
	if (!IS_ENABLED(CONFIG_DEBUG_BRINGUP))
		MCHP_PCR_SYS_RST |= MCHP_PCR_SYS_SOFT_RESET;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

void system_reset(int flags)
{
	_system_reset(flags, 0);
}

const char *system_get_chip_vendor(void)
{
	return "mchp";
}

#ifdef CHIP_VARIANT_MEC1701
/*
 * MEC1701H Chip ID = 0x2D
 *              Rev = 0x82
 */
const char *system_get_chip_name(void)
{
	switch (MCHP_CHIP_DEV_ID) {
	case 0x2D:
		return "mec1701";
	default:
		return "unknown";
	}
}
#endif

#ifdef CHIP_FAMILY_MEC152X
/*
 * MEC152x family implements chip ID as a 32-bit
 * register where:
 * b[31:16] = 16-bit Device ID
 * b[15:8] = 8-bit Sub ID
 * b[7:0] = Revision
 *
 * MEC1521-128 WFBGA 0023_33_xxh
 * MEC1521-144 WFBGA 0023_34_xxh
 * MEC1523-144 WFBGA 0023_B4_xxh
 * MEC1527-144 WFBGA 0023_74_xxh
 * MEC1527-128 WFBGA 0023_73_xxh
 */
const char *system_get_chip_name(void)
{
	switch (MCHP_CHIP_DEVRID32 & ~(MCHP_CHIP_REV_MASK)) {
	case 0x00201400: /* 144 pin rev A? */
		return "mec1503_revA";
	case 0x00203400: /* 144 pin */
		return "mec1501";
	case 0x00207400: /* 144 pin */
		return "mec1507";
	case 0x00208400: /* 144 pin */
		return "mec1503";
	case 0x00233300: /* 128 pin */
	case 0x00233400: /* 144 pin */
		return "mec1521";
	case 0x0023B400: /* 144 pin */
		return "mec1523";
	case 0x00237300: /* 128 pin */
	case 0x00237400: /* 144 pin */
		return "mec1527";
	default:
		return "unknown";
	}
}
#endif

#ifdef CHIP_FAMILY_MEC172X
/*
 * MEC172x family implements chip ID as a 32-bit
 * register where:
 * b[31:16] = 16-bit Device ID
 * b[15:8] = 8-bit Sub ID
 * b[7:0] = Revision
 *
 * MEC1723N-B0-I/SZ 144 pin: 0x0022_34_xx
 * MEC1727N-B0-I/SZ 144 pin: 0x0022_74_xx
 * MEC1721N-B0-I/LJ 176 pin: 0x0022_27_xx
 * MEC1723N-B0-I/LJ 176 pin: 0x0022_37_xx
 * MEC1727N-B0-I/LJ 176 pin: 0x0022_77_xx
 */
const char *system_get_chip_name(void)
{
	switch (MCHP_CHIP_DEVRID32 & ~(MCHP_CHIP_REV_MASK)) {
	case 0x00223400:
		return "MEC1723NSZ";
	case 0x00227400:
		return "MEC1727NSZ";
	case 0x00222700:
		return "MEC1721NLJ";
	case 0x00223700:
		return "MEC1723NLJ";
	case 0x00227700:
		return "MEC1727NLJ";
	default:
		return "unknown";
	}
}
#endif

static char to_hex(int x)
{
	if (x >= 0 && x <= 9)
		return '0' + x;
	return 'a' + x - 10;
}

const char *system_get_chip_revision(void)
{
	static char buf[3];
	uint8_t rev = MCHP_CHIP_DEV_REV;

	buf[0] = to_hex(rev / 16);
	buf[1] = to_hex(rev & 0xf);
	buf[2] = '\0';
	return buf;
}

static int bbram_idx_lookup(enum system_bbram_idx idx)
{
	switch (idx) {
	case SYSTEM_BBRAM_IDX_PD0:
		return HIBDATA_INDEX_PD0;
	case SYSTEM_BBRAM_IDX_PD1:
		return HIBDATA_INDEX_PD1;
	case SYSTEM_BBRAM_IDX_PD2:
		return HIBDATA_INDEX_PD2;
	default:
		return 1;
	}
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int hibdata = bbram_idx_lookup(idx);

	if (hibdata < 0)
		return EC_ERROR_UNIMPLEMENTED;

	*value = MCHP_VBAT_RAM(hibdata);
	return EC_SUCCESS;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	int hibdata = bbram_idx_lookup(idx);

	if (hibdata < 0)
		return EC_ERROR_UNIMPLEMENTED;

	MCHP_VBAT_RAM(hibdata) = value;
	return EC_SUCCESS;
}

int system_set_scratchpad(uint32_t value)
{
	MCHP_VBAT_RAM(HIBDATA_INDEX_SCRATCHPAD) = value;
	return EC_SUCCESS;
}

int system_get_scratchpad(uint32_t *value)
{
	*value = MCHP_VBAT_RAM(HIBDATA_INDEX_SCRATCHPAD);
	return EC_SUCCESS;
}

/*
 * Local function to disable clocks in the chip's host interface
 * so the chip can enter deep sleep. Only MEC170X has LPC.
 * MEC152x and MEC172x only include eSPI and SPI host interfaces.
 * NOTE: we do it this way because the LPC registers are only
 * defined for MEC170x and the IS_ENABLED() macro causes the
 * compiler to evaluate both true and false code paths.
 */
#if defined(CONFIG_HOST_INTERFACE_ESPI)
static void disable_host_ifc_clocks(void)
{
	MCHP_ESPI_ACTIVATE &= ~0x01;
}
#else
static void disable_host_ifc_clocks(void)
{
#ifdef CHIP_FAMILY_MEC170X
	MCHP_LPC_ACT &= ~0x1;
#endif
}
#endif

/*
 * Called when hibernation timer is not used in deep sleep.
 * Switch 32 KHz clock logic from external 32KHz input to
 * internal silicon OSC.
 * NOTE: MEC172x auto-switches from external source to silicon
 * oscillator.
 */
#ifdef CHIP_FAMILY_MEC172X
static void switch_32k_pin2sil(void)
{
}
#else
static void switch_32k_pin2sil(void)
{
	MCHP_VBAT_CE &= ~(MCHP_VBAT_CE_32K_DOMAIN_32KHZ_IN_PIN);
}
#endif

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	int i;

	if (IS_ENABLED(CONFIG_HOSTCMD_PD)) {
		/* Inform the PD MCU that we are going to hibernate. */
		host_command_pd_request_hibernate();
		/* Wait to ensure exchange with PD before hibernating. */
		crec_msleep(100);
	}

	cflush();

	if (board_hibernate)
		board_hibernate();

	/* Disable interrupts */
	interrupt_disable();
	for (i = 0; i < MCHP_IRQ_MAX; ++i) {
		task_disable_irq(i);
		task_clear_pending_irq(i);
	}

	for (i = MCHP_INT_GIRQ_FIRST; i <= MCHP_INT_GIRQ_LAST; ++i) {
		MCHP_INT_DISABLE(i) = 0xffffffff;
		MCHP_INT_SOURCE(i) = 0xffffffff;
	}

	/* Disable UART */
	MCHP_UART_ACT(0) &= ~0x1;

	disable_host_ifc_clocks();

	/* Disable JTAG */
	MCHP_EC_JTAG_EN &= ~1;

	/* Stop watchdog */
	MCHP_WDG_CTL &= ~(MCHP_WDT_CTL_ENABLE);

	/* Stop timers */
	MCHP_TMR32_CTL(0) &= ~1;
	MCHP_TMR32_CTL(1) &= ~1;
	for (i = 0; i < MCHP_TMR16_INSTANCES; i++)
		MCHP_TMR16_CTL(i) &= ~1;

	/* Power down ADC */
	/*
	 * If ADC is in middle of acquisition it will continue until finished
	 */
	MCHP_ADC_CTRL &= ~1;

	/* Disable blocks */
	MCHP_PCR_SLOW_CLK_CTL &= ~(MCHP_PCR_SLOW_CLK_CTL_MASK);

	/* Setup GPIOs for hibernate */
	if (board_hibernate_late)
		board_hibernate_late();

	if (hibernate_wake_pins_used > 0) {
		for (i = 0; i < hibernate_wake_pins_used; ++i) {
			const enum gpio_signal pin = hibernate_wake_pins[i];

			gpio_reset(pin);
			gpio_enable_interrupt(pin);
		}

		interrupt_enable();
		task_enable_irq(MCHP_IRQ_GIRQ8);
		task_enable_irq(MCHP_IRQ_GIRQ9);
		task_enable_irq(MCHP_IRQ_GIRQ10);
		task_enable_irq(MCHP_IRQ_GIRQ11);
		task_enable_irq(MCHP_IRQ_GIRQ12);
		task_enable_irq(MCHP_IRQ_GIRQ26);
	}

	if (seconds || microseconds) {
		htimer_init();
		system_set_htimer_alarm(seconds, microseconds);
		interrupt_enable();
	} else
		switch_32k_pin2sil();

	/*
	 * Set sleep state
	 * arm sleep state to trigger on next WFI
	 */
	CPU_SCB_SYSCTRL |= 0x4;
	MCHP_PCR_SYS_SLP_CTL = MCHP_PCR_SYS_SLP_HEAVY;
	MCHP_PCR_SYS_SLP_CTL = MCHP_PCR_SYS_SLP_ALL;

	asm("dsb");
	cpu_enter_suspend_mode();
	asm("isb");
	asm("nop");

	/* Use fastest clock to speed through wake-up */
	MCHP_PCR_PROC_CLK_CTL = MCHP_PCR_CLK_CTL_FASTEST;

	/* Reboot */
	_system_reset(0, 1);

	/* We should never get here. */
	while (1)
		;
}

static void htimer_interrupt(void)
{
	/* Time to wake up */
}
DECLARE_IRQ(MCHP_IRQ_HTIMER0, htimer_interrupt, 1);

enum ec_image system_get_shrspi_image_copy(void)
{
	return MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX);
}

uint32_t system_get_lfw_address(void)
{
	uint32_t *const lfw_vector =
		(uint32_t *const)CONFIG_PROGRAM_MEMORY_BASE;

	return *(lfw_vector + 1);
}

void system_set_image_copy(enum ec_image copy)
{
	MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX) =
		(copy == EC_IMAGE_RW) ? EC_IMAGE_RW : EC_IMAGE_RO;
}
