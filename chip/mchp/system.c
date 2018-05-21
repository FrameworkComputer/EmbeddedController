/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : MCHP hardware specific implementation */

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
#include "lpc_chip.h"
#include "tfdp_chip.h"


#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)


/* Indices for hibernate data registers (RAM backed by VBAT) */
enum hibdata_index {
	HIBDATA_INDEX_SCRATCHPAD = 0,    /* General-purpose scratchpad */
	HIBDATA_INDEX_SAVED_RESET_FLAGS, /* Saved reset flags */
	HIBDATA_INDEX_PD0,		/* USB-PD0 saved port state */
	HIBDATA_INDEX_PD1,		/* USB-PD1 saved port state */
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
		flags |= RESET_FLAG_RESET_PIN;


	flags |= MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS);
	MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS) = 0;

	if ((status & MCHP_VBAT_STS_WDT) && !(flags & (RESET_FLAG_SOFT |
					    RESET_FLAG_HARD |
					    RESET_FLAG_HIBERNATE)))
		flags |= RESET_FLAG_WATCHDOG;

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

	if ((reset_flags & RESET_FLAG_RESET_PIN) ||
		(reset_flags & RESET_FLAG_POWER_ON) ||
		(reset_flags & RESET_FLAG_WATCHDOG) ||
		(reset_flags & RESET_FLAG_HARD) ||
		(reset_flags & RESET_FLAG_SOFT))
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
	uint32_t d;

	d = MCHP_PCR_SLP_EN0_SLEEP;
#ifdef CONFIG_CHIPSET_DEBUG
	d &= ~(MCHP_PCR_SLP_EN0_JTAG);
#ifdef CONFIG_MCHP_JTAG_MODE
	MCHP_EC_JTAG_EN = CONFIG_MCHP_JTAG_MODE;
#else
	MCHP_EC_JTAG_EN |= 0x01;
#endif
#else
	MCHP_EC_JTAG_EN &= ~0x01;
#endif
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
}
#endif

void system_pre_init(void)
{
#ifdef CONFIG_MCHP_TFDP
	uint8_t imgtype;
#endif

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

#ifdef CONFIG_MCHP_TFDP
	/*
	 * MCHP Enable TFDP for fast debug messages
	 * If not defined then traceN() and TRACEN() macros are empty
	 */
	tfdp_power(1);
	tfdp_enable(1, 1);
	imgtype = MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX);
	CPRINTS("system_pre_init. Image type = 0x%02x", imgtype);
	trace1(0, MEC, 0, "System pre-init. Image type = 0x%02x", imgtype);

	/* Debug: dump some signals */
	imgtype = gpio_get_level(GPIO_PCH_RSMRST_L);
	trace1(0, MEC, 0, "PCH_RSMRST_L = %d", imgtype);

	imgtype = gpio_get_level(GPIO_RSMRST_L_PGOOD);
	trace1(0, MEC, 0, "RSMRST_L_PGOOD = %d", imgtype);
	imgtype = gpio_get_level(GPIO_POWER_BUTTON_L);
	trace1(0, MEC, 0, "POWER_BUTTON_L = %d", imgtype);
	imgtype = gpio_get_level(GPIO_PMIC_DPWROK);
	trace1(0, MEC, 0, "PMIC_DPWROK = %d", imgtype);
	imgtype = gpio_get_level(GPIO_ALL_SYS_PWRGD);
	trace1(0, MEC, 0, "ALL_SYS_PWRGD = %d", imgtype);
	imgtype = gpio_get_level(GPIO_AC_PRESENT);
	trace1(0, MEC, 0, "AC_PRESENT = %d", imgtype);
	imgtype = gpio_get_level(GPIO_PCH_SLP_SUS_L);
	trace1(0, MEC, 0, "PCH_SLP_SUS_L = %d", imgtype);
	imgtype = gpio_get_level(GPIO_PMIC_INT_L);
	trace1(0, MEC, 0, "PCH_PMIC_INT_L = %d", imgtype);
#endif
}

void chip_save_reset_flags(int flags)
{
	MCHP_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS) = flags;
}

void __attribute__((noreturn)) _system_reset(int flags,
					int wake_from_hibernate)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	trace12(0, MEC, 0,
		"_system_reset: flags=0x%08X  wake_from_hibernate=%d",
		flags, wake_from_hibernate);

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	if (wake_from_hibernate)
		save_flags |= RESET_FLAG_HIBERNATE;
	else if (flags & SYSTEM_RESET_HARD)
		save_flags |= RESET_FLAG_HARD;
	else
		save_flags |= RESET_FLAG_SOFT;

	chip_save_reset_flags(save_flags);

	trace11(0, MEC, 0, "_system_reset: save_flags=0x%08X", save_flags);

	/*
	 * Trigger chip reset
	 */
#ifndef CONFIG_CHIPSET_DEBUG
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
	switch (idx) {
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case SYSTEM_BBRAM_IDX_PD0:
		return HIBDATA_INDEX_PD0;
	case SYSTEM_BBRAM_IDX_PD1:
		return HIBDATA_INDEX_PD1;
#endif
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
	for (i = 0; i <= 92; ++i) {
		task_disable_irq(i);
		task_clear_pending_irq(i);
	}

	for (i = 8; i <= 26; ++i)
		MCHP_INT_DISABLE(i) = 0xffffffff;

	MCHP_INT_BLK_DIS |= 0xffff00;

	/* Disable UART */
	MCHP_UART_ACT(0) &= ~0x1;
	MCHP_LPC_ACT &= ~0x1;

	/* Disable JTAG */
	MCHP_EC_JTAG_EN &= ~1;

	/* Disable 32KHz clock */
	MCHP_VBAT_CE &= ~0x2;

	/* Stop watchdog */
	MCHP_WDG_CTL &= ~1;

	/* Stop timers */
	MCHP_TMR32_CTL(0) &= ~1;
	MCHP_TMR32_CTL(1) &= ~1;
	MCHP_TMR16_CTL(0) &= ~1;

	/* Power down ADC */
	/*
	 * If ADC is in middle of acquisition it will continue until finished
	 */
	MCHP_ADC_CTRL &= ~1;

	/* Disable blocks */
	MCHP_PCR_SLOW_CLK_CTL &= ~(MCHP_PCR_SLOW_CLK_CTL_MASK);

	/*
	 * Set sleep state
	 * arm sleep state to trigger on next WFI
	 */
	CPU_SCB_SYSCTRL |= 0x4;
	MCHP_PCR_SYS_SLP_CTL = MCHP_PCR_SYS_SLP_HEAVY;
	MCHP_PCR_SYS_SLP_CTL = MCHP_PCR_SYS_SLP_ALL;

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
	}

	if (seconds || microseconds) {
		/*
		 * Not needed when using direct mode interrupts.
		 * MCHP_INT_BLK_EN |= 1 << MCHP_HTIMER_GIRQ;
		 */
		MCHP_INT_ENABLE(MCHP_HTIMER_GIRQ) =
				MCHP_HTIMER_GIRQ_BIT(0);
		interrupt_enable();
		task_enable_irq(MCHP_IRQ_HTIMER0);
		if (seconds > 2) {
			ASSERT(seconds <= 0xffff / 8);
			MCHP_HTIMER_CONTROL(0) = 1;
			MCHP_HTIMER_PRELOAD(0) =
				(seconds * 8 + microseconds / 125000);
		} else {
			MCHP_HTIMER_CONTROL(0) = 0;
			MCHP_HTIMER_PRELOAD(0) =
				(seconds * 1000000 + microseconds) * 2 / 71;
		}
	}

	asm("wfi");

	/* Use 48MHz clock to speed through wake-up */
	MCHP_PCR_PROC_CLK_CTL = 1;

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

enum system_image_copy_t system_get_shrspi_image_copy(void)
{
	return MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX);
}

uint32_t system_get_lfw_address(void)
{
	uint32_t * const lfw_vector =
		(uint32_t * const)CONFIG_PROGRAM_MEMORY_BASE;

	return *(lfw_vector + 1);
}

void system_set_image_copy(enum system_image_copy_t copy)
{
	MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX) = (copy == SYSTEM_IMAGE_RW) ?
				SYSTEM_IMAGE_RW : SYSTEM_IMAGE_RO;
}

