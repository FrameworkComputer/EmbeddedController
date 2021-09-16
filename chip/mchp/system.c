/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : MCHP hardware specific implementation */

#include <stdnoreturn.h>

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "host_command.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "spi.h"
#include "clock_chip.h"
#include "lpc_chip.h"
#include "tfdp_chip.h"


#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)


/* Indices for hibernate data registers (RAM backed by VBAT) total 64byte */
enum hibdata_index {
	/* use for word */
	HIBDATA_INDEX_SCRATCHPAD = 0,    /* General-purpose scratchpad */
	HIBDATA_INDEX_SAVED_RESET_FLAGS = 4, /* Saved reset flags */
	/* use for byte */
#ifdef CONFIG_HOSTCMD_VBNV_CONTEXT
	HIBDATA_INDEX_VBNVCNTXT = 8, /* total 16 byte 8 ~ 23 */
#endif
	HIBDATA_INDEX_PD0 = 24,		/* USB-PD0 saved port state */
	HIBDATA_INDEX_PD1 = 25,		/* USB-PD1 saved port state */
	HIBDATA_INDEX_PD2 = 26,		/* USB-PD2 saved port state */
	HIBDATA_INDEX_PD3 = 27,		/* USB-PD3 saved port state */
	HIBDATA_INDEX_CHG_MAX = 28,
	HIBDATA_INDEX_AC_BOOT = 29,
	HIBDATA_INDEX_TRY_SLOT = 30,
	HIBDATA_INDEX_KBSTATE = 31,
	HIBDATA_INDEX_CHASSIS_TOTAL = 32,
	HIBDATA_INDEX_CHASSIS_MAGIC = 33,
	HIBDATA_INDEX_CHASSIS_VTR_OPEN = 34,
	HIBDATA_INDEX_VPRO_STATUS = 35,
	HIBDATA_INDEX_CHASSIS_WAS_OPEN = 36,
	HIBDATA_INDEX_FP_LED_LEVEL = 37,
	/*
	 * .. 56 ~ 59 byte for ESPI VW use ..
	 * .. 60 ~ 63 byte for IMAGETYPE use ..
	 */
};

static void check_reset_cause(void)
{
	uint32_t status = MCHP_VBAT_STS;
	uint32_t flags = 0;
	uint32_t rst_sts = MCHP_PCR_PWR_RST_STS &
				(MCHP_PWR_RST_STS_VTR |
				MCHP_PWR_RST_STS_VBAT);

	trace12(0, MEC, 0,
		"check_reset_cause: VBAT_PFR = 0x%08X  PCR PWRST = 0x%08X",
		status, rst_sts);

	/* Clear the reset causes now that we've read them */
	MCHP_VBAT_STS |= status;
	MCHP_PCR_PWR_RST_STS |= rst_sts;

	trace0(0, MEC, 0, "check_reset_cause: after clear");
	trace11(0, MEC, 0, "  VBAT_PFR  = 0x%08X", MCHP_VBAT_STS);
	trace11(0, MEC, 0, "  PCR PWRST = 0x%08X", MCHP_PCR_PWR_RST_STS);

	/*
	 * BIT[6] determine VTR reset
	 */
	if (rst_sts & MCHP_PWR_RST_STS_VTR)
		flags |= EC_RESET_FLAG_RESET_PIN;


	flags |= chip_read_reset_flags();
	chip_save_reset_flags(0);

	if ((status & MCHP_VBAT_STS_WDT) && !(flags & (EC_RESET_FLAG_SOFT |
					    EC_RESET_FLAG_HARD |
					    EC_RESET_FLAG_HIBERNATE)))
		flags |= EC_RESET_FLAG_WATCHDOG;

	trace11(0, MEC, 0, "check_reset_cause: EC reset flags = 0x%08x", flags);

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
 * Drivers/modules will unsleep their blocks.
 * Keep sleep enables cleared for required blocks:
 * ECIA, PMC, CPU, ECS and optionally JTAG.
 * SLEEP_ALL feature will set these upon sleep entry.
 * Based on CONFIG_CHIPSET_DEBUG enable or disable JTAG.
 * JTAG mode (4-pin or 2-pin SWD + 1-pin SWV) was set
 * by Boot-ROM. We can override Boot-ROM JTAG mode
 * using
 * CONFIG_MCHP_JTAG_MODE
 */
static void chip_periph_sleep_control(void)
{
#ifdef CONFIG_CHIPSET_DEBUG
	uint32_t d;

	d = MCHP_PCR_SLP_EN0_SLEEP;

	d &= ~(MCHP_PCR_SLP_EN0_JTAG);
#ifdef CONFIG_MCHP_JTAG_MODE
	MCHP_EC_JTAG_EN = CONFIG_MCHP_JTAG_MODE;
#else
	MCHP_EC_JTAG_EN |= 0x01;
#endif
#else
	MCHP_EC_JTAG_EN &= ~0x01;
#endif

#ifdef CHIP_FAMILY_MEC17XX
	MCHP_PCR_SLP_EN0 = d;
#endif 
	MCHP_PCR_SLP_EN1 = MCHP_PCR_SLP_EN1_UNUSED_BLOCKS;
	MCHP_PCR_SLP_EN2 = MCHP_PCR_SLP_EN2_SLEEP;
	MCHP_PCR_SLP_EN3 = MCHP_PCR_SLP_EN3_SLEEP;
	MCHP_PCR_SLP_EN4 = MCHP_PCR_SLP_EN4_SLEEP;
}

#ifdef CONFIG_CHIP_PRE_INIT
void chip_pre_init(void)
{
#ifdef CONFIG_MCHP_TFDP
	uint8_t imgtype;
#endif
	chip_periph_sleep_control();

#ifdef CONFIG_MCHP_TFDP
	/*
	 * MCHP Enable TFDP for fast debug messages
	 * If not defined then traceN() and TRACEN() macros are empty
	 */
	tfdp_power(1);
	tfdp_enable(1, 1);
	imgtype = MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX);
	CPRINTS("chip_pre_init: Image type = 0x%02x", imgtype);
	trace1(0, MEC, 0,
		"chip_pre_init: Image type = 0x%02x", imgtype);

	trace11(0, MEC, 0,
		"chip_pre_init: MCHP_VBAT_STS = 0x%0x",
		MCHP_VBAT_STS);
	trace11(0, MEC, 0,
		"chip_pre_init: MCHP_PCR_PWR_RST_STS = 0x%0x",
		MCHP_VBAT_STS);
#endif
}
#endif

void system_pre_init(void)
{
	/*
	 * Make sure AHB Error capture is enabled.
	 * Signals bus fault to Cortex-M4 core if an address presented
	 * to AHB is not claimed by any HW block.
	 */
	MCHP_EC_AHB_ERR = 0;	/* write any value to clear */
	MCHP_EC_AHB_ERR_EN = 0; /* enable capture of address on error */

#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_EC_GPIO_BANK_PWR |= MCHP_EC_GPIO_BANK_PWR_VTR3_18;
#endif

#ifndef CONFIG_CHIP_PRE_INIT
	chip_periph_sleep_control();
#endif

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
	MCHP_INT_BLK_EN = (0x1Ful << 8) + (0x07ul << 24);

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

uint32_t chip_read_reset_flags(void)
{
	return MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS);
}

void chip_save_reset_flags(uint32_t flags)
{
	MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS) = flags;
}

noreturn void _system_reset(int flags, int wake_from_hibernate)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	trace12(0, MEC, 0,
		"_system_reset: flags=0x%08X  wake_from_hibernate=%d",
		flags, wake_from_hibernate);

	/* According to the spec chassis pin(BIT 2) will affect
	 * VCI_OUT pin, so when do chipset reset need to set it back
	 * at boot also will set up at VCI init
	 */
	if (MCHP_VCI_INPUT_ENABLE & BIT(2))
		MCHP_VCI_INPUT_ENABLE &= ~BIT(2);

	if (MCHP_VCI_BUFFER_EN & BIT(2))
		MCHP_VCI_BUFFER_EN &= ~BIT(2);

	/* make sure next boot up register states is clear */
	MCHP_VCI_NEGEDGE_DETECT = BIT(0) |  BIT(1);
	MCHP_VCI_POSEDGE_DETECT = BIT(0) |  BIT(1);

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

	trace11(0, MEC, 0, "_system_reset: save_flags=0x%08X", save_flags);

	/*
	 * Trigger chip reset
	 */
#if defined(CONFIG_CHIPSET_DEBUG)
#else
	MCHP_PCR_SYS_RST |= MCHP_PCR_SYS_SOFT_RESET;
#endif
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

#ifdef CONFIG_HOSTCMD_VBNV_CONTEXT
	if (idx >= SYSTEM_BBRAM_IDX_VBNVBLOCK0 &&
	    idx <= SYSTEM_BBRAM_IDX_VBNVBLOCK15)
		return idx + HIBDATA_INDEX_VBNVCNTXT;
#endif

	switch (idx) {
	case SYSTEM_BBRAM_IDX_PD0:
		return HIBDATA_INDEX_PD0;
	case SYSTEM_BBRAM_IDX_PD1:
		return HIBDATA_INDEX_PD1;
	case SYSTEM_BBRAM_IDX_PD2:
		return HIBDATA_INDEX_PD2;
	case SYSTEM_BBRAM_IDX_PD3:
		return HIBDATA_INDEX_PD3;
	case SYSTEM_BBRAM_IDX_CHG_MAX:
		return HIBDATA_INDEX_CHG_MAX;
	case SYSTEM_BBRAM_IDX_AC_BOOT:
		return HIBDATA_INDEX_AC_BOOT;
	case SYSTEM_BBRAM_IDX_TRY_SLOT:
		return HIBDATA_INDEX_TRY_SLOT;
	case SYSTEM_BBRAM_IDX_KBSTATE:
		return HIBDATA_INDEX_KBSTATE;
	case SYSTEM_BBRAM_IDX_CHASSIS_TOTAL:
		return HIBDATA_INDEX_CHASSIS_TOTAL;
	case STSTEM_BBRAM_IDX_CHASSIS_MAGIC:
		return HIBDATA_INDEX_CHASSIS_MAGIC;
	case STSTEM_BBRAM_IDX_CHASSIS_VTR_OPEN:
		return HIBDATA_INDEX_CHASSIS_VTR_OPEN;
	case SYSTEM_BBRAM_IDX_VPRO_STATUS:
		return HIBDATA_INDEX_VPRO_STATUS;
	case STSTEM_BBRAM_IDX_CHASSIS_WAS_OPEN:
		return HIBDATA_INDEX_CHASSIS_WAS_OPEN;
	case STSTEM_BBRAM_IDX_FP_LED_LEVEL:
		return HIBDATA_INDEX_FP_LED_LEVEL;
	default:
		return -1;
	}
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int hibdata = bbram_idx_lookup(idx);

	if (hibdata < 0)
		return EC_ERROR_UNIMPLEMENTED;

	if (idx != hibdata)
		*value = MCHP_VBAT_RAM8(hibdata);
	else
		*value = MCHP_VBAT_RAM(hibdata);

	return EC_SUCCESS;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	int hibdata = bbram_idx_lookup(idx);

	if (hibdata < 0)
		return EC_ERROR_UNIMPLEMENTED;

	if (idx != hibdata)
		MCHP_VBAT_RAM8(hibdata) = value;
	else
		MCHP_VBAT_RAM(hibdata) = value;

	return EC_SUCCESS;
}

int system_set_scratchpad(uint32_t value)
{
	MCHP_VBAT_RAM(HIBDATA_INDEX_SCRATCHPAD) = value;
	return EC_SUCCESS;
}

uint32_t system_get_scratchpad(void)
{
	return MCHP_VBAT_RAM(HIBDATA_INDEX_SCRATCHPAD);
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	int i;

#ifdef CONFIG_HOSTCMD_PD
	/* Inform the PD MCU that we are going to hibernate. */
	host_command_pd_request_hibernate();
	/* Wait to ensure exchange with PD before hibernating. */
	msleep(100);
#endif

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
		MCHP_INT_SOURCE(i)  = 0xffffffff;
	}

	/* Disable UART */
	MCHP_UART_ACT(0) &= ~0x1;
#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_ESPI_ACTIVATE &= ~0x01;
#else
	MCHP_LPC_ACT &= ~0x1;
#endif
	/* Disable JTAG */
	MCHP_EC_JTAG_EN &= ~1;

	/* Stop watchdog */
	MCHP_WDG_CTL &= ~1;

	/* Stop timers */
	MCHP_TMR32_CTL(0) &= ~1;
	MCHP_TMR32_CTL(1) &= ~1;
	for (i = 0; i < MCHP_TMR16_MAX; i++)
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
	} else {
		/* Not using hibernation timer. Disable 32KHz clock */
		MCHP_VBAT_CE &= ~0x2;
	}

	/*
	 * Set sleep state
	 * arm sleep state to trigger on next WFI
	 */
	CPU_SCB_SYSCTRL |= 0x4;
	MCHP_PCR_SYS_SLP_CTL = MCHP_PCR_SYS_SLP_HEAVY;
	MCHP_PCR_SYS_SLP_CTL |= MCHP_PCR_SYS_SLP_ALL;

	asm("dsb");
	asm("wfi");
	asm("isb");
	asm("nop");

	/* Use 48MHz clock to speed through wake-up */
	MCHP_PCR_PROC_CLK_CTL = 1;

	trace0(0, SYS, 0, "Wake from hibernate: _system_reset[0,1]");

	/* Reboot */
	_system_reset(0, 1);

	/* We should never get here. */
	while (1)
		;
}

void htimer_interrupt(void)
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
	uint32_t * const lfw_vector =
		(uint32_t * const)CONFIG_PROGRAM_MEMORY_BASE;

	return *(lfw_vector + 1);
}

void system_set_image_copy(enum ec_image copy)
{
	MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX) = (copy == EC_IMAGE_RW) ?
				EC_IMAGE_RW : EC_IMAGE_RO;
}

