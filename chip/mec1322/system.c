/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : MEC1322 hardware specific implementation */

#include "builtin/assert.h"
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
#include "usb_pd.h"
#include "util.h"
#include "spi.h"

/* Indices for hibernate data registers (RAM backed by VBAT) */
enum hibdata_index {
	HIBDATA_INDEX_SCRATCHPAD = 0, /* General-purpose scratchpad */
	HIBDATA_INDEX_SAVED_RESET_FLAGS, /* Saved reset flags */
	HIBDATA_INDEX_PD0, /* USB-PD0 saved port state */
	HIBDATA_INDEX_PD1, /* USB-PD1 saved port state */
	HIBDATA_INDEX_PD2, /* USB-PD2 saved port state */
};

static void check_reset_cause(void)
{
	uint32_t status = MEC1322_VBAT_STS;
	uint32_t flags = 0;
	uint32_t rst_sts =
		MEC1322_PCR_CHIP_PWR_RST &
		(MEC1322_PWR_RST_STS_VCC1 | MEC1322_PWR_RST_STS_VBAT);

	/* Clear the reset causes now that we've read them */
	MEC1322_VBAT_STS |= status;
	MEC1322_PCR_CHIP_PWR_RST |= rst_sts;

	/*
	 * BIT[6] determine VCC1 reset
	 */
	if (rst_sts & MEC1322_PWR_RST_STS_VCC1)
		flags |= EC_RESET_FLAG_RESET_PIN;

	flags |= MEC1322_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS);
	MEC1322_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS) = 0;

	if ((status & MEC1322_VBAT_STS_WDT) &&
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
	    (reset_flags & EC_RESET_FLAG_SOFT) ||
	    (reset_flags & EC_RESET_FLAG_HIBERNATE))
		return 0;
	else
		return 1;
}

void system_pre_init(void)
{
	/* Enable direct NVIC */
	MEC1322_EC_INT_CTRL |= 1;

	/* Disable ARM TRACE debug port */
	MEC1322_EC_TRACE_EN &= ~1;

	/* Deassert nSIO_RESET */
	MEC1322_PCR_PWR_RST_CTL &= ~BIT(0);

	spi_enable(SPI_FLASH_DEVICE, 1);
}

void chip_save_reset_flags(uint32_t flags)
{
	MEC1322_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS) = flags;
}

uint32_t chip_read_reset_flags(void)
{
	return MEC1322_VBAT_RAM(HIBDATA_INDEX_SAVED_RESET_FLAGS);
}

noreturn void _system_reset(int flags, int wake_from_hibernate)
{
	uint32_t save_flags = 0;

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

	/* Trigger watchdog in 1ms */
	MEC1322_WDG_LOAD = 1;
	MEC1322_WDG_CTL |= 1;

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
	return "smsc";
}

const char *system_get_chip_name(void)
{
	switch (MEC1322_CHIP_DEV_ID) {
	case 0x15:
		return "mec1322";
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
	uint8_t rev = MEC1322_CHIP_DEV_REV;

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
		return -1;
	}
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int hibdata = bbram_idx_lookup(idx);

	if (hibdata < 0)
		return EC_ERROR_UNIMPLEMENTED;

	*value = MEC1322_VBAT_RAM(hibdata);
	return EC_SUCCESS;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	int hibdata = bbram_idx_lookup(idx);

	if (hibdata < 0)
		return EC_ERROR_UNIMPLEMENTED;

	MEC1322_VBAT_RAM(hibdata) = value;
	return EC_SUCCESS;
}

int system_set_scratchpad(uint32_t value)
{
	MEC1322_VBAT_RAM(HIBDATA_INDEX_SCRATCHPAD) = value;
	return EC_SUCCESS;
}

int system_get_scratchpad(uint32_t *value)
{
	*value = MEC1322_VBAT_RAM(HIBDATA_INDEX_SCRATCHPAD);
	return EC_SUCCESS;
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

	for (i = 8; i <= 23; ++i)
		MEC1322_INT_DISABLE(i) = 0xffffffff;

	MEC1322_INT_BLK_DIS |= 0xffff00;

	/* Power down ADC VREF */
	MEC1322_EC_ADC_VREF_PD |= 1;

	/* Assert nSIO_RESET */
	MEC1322_PCR_PWR_RST_CTL |= 1;

	/* Disable UART */
	MEC1322_UART_ACT &= ~0x1;
	MEC1322_LPC_ACT &= ~0x1;

	/* Disable JTAG */
	MEC1322_EC_JTAG_EN &= ~1;

	/* Disable 32KHz clock */
	MEC1322_VBAT_CE &= ~0x2;

	/* Stop watchdog */
	MEC1322_WDG_CTL &= ~1;

	/* Stop timers */
	MEC1322_TMR32_CTL(0) &= ~1;
	MEC1322_TMR32_CTL(1) &= ~1;
	MEC1322_TMR16_CTL(0) &= ~1;

	/* Power down ADC */
	MEC1322_ADC_CTRL &= ~1;

	/* Disable blocks */
	MEC1322_PCR_CHIP_SLP_EN |= 0x3;
	MEC1322_PCR_EC_SLP_EN |= MEC1322_PCR_EC_SLP_EN_SLEEP;
	MEC1322_PCR_HOST_SLP_EN |= MEC1322_PCR_HOST_SLP_EN_SLEEP;
	MEC1322_PCR_EC_SLP_EN2 |= MEC1322_PCR_EC_SLP_EN2_SLEEP;
	MEC1322_PCR_SLOW_CLK_CTL &= 0xfffffc00;

	/* Set sleep state */
	MEC1322_PCR_SYS_SLP_CTL = (MEC1322_PCR_SYS_SLP_CTL & ~0x7) | 0x2;
	CPU_SCB_SYSCTRL |= 0x4;

	/* Setup GPIOs for hibernate */
	if (board_hibernate_late)
		board_hibernate_late();

#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
	/*
	 * Leave USB-C charging enabled in hibernate, in order to
	 * allow wake-on-plug. 5V enable must be pulled low.
	 */
	switch (board_get_usb_pd_port_count()) {
#if CONFIG_USB_PD_PORT_MAX_COUNT >= 2
	case 2:
		gpio_set_flags(GPIO_USB_C1_5V_EN, GPIO_PULL_DOWN | GPIO_INPUT);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 0);
		/* Fall through */
#endif
#if CONFIG_USB_PD_PORT_MAX_COUNT >= 1
	case 1:
		gpio_set_flags(GPIO_USB_C0_5V_EN, GPIO_PULL_DOWN | GPIO_INPUT);
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 0);
		/* Fall through */
#endif
	case 0:
		/* Nothing to do but break */
		break;
	default:
		/* More ports needs to be defined */
		ASSERT(false);
		break;
	}
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */

	if (hibernate_wake_pins_used > 0) {
		for (i = 0; i < hibernate_wake_pins_used; ++i) {
			const enum gpio_signal pin = hibernate_wake_pins[i];

			gpio_reset(pin);
			gpio_enable_interrupt(pin);
		}

		interrupt_enable();
		task_enable_irq(MEC1322_IRQ_GIRQ8);
		task_enable_irq(MEC1322_IRQ_GIRQ9);
		task_enable_irq(MEC1322_IRQ_GIRQ10);
		task_enable_irq(MEC1322_IRQ_GIRQ11);
		task_enable_irq(MEC1322_IRQ_GIRQ20);
	}

	if (seconds || microseconds) {
		MEC1322_INT_BLK_EN |= BIT(17);
		MEC1322_INT_ENABLE(17) |= BIT(20);
		interrupt_enable();
		task_enable_irq(MEC1322_IRQ_HTIMER);
		if (seconds > 2) {
			ASSERT(seconds <= 0xffff / 8);
			MEC1322_HTIMER_CONTROL = 1;
			MEC1322_HTIMER_PRELOAD =
				(seconds * 8 + microseconds / 125000);
		} else {
			MEC1322_HTIMER_CONTROL = 0;
			MEC1322_HTIMER_PRELOAD =
				(seconds * 1000000 + microseconds) * 2 / 71;
		}
	}

	cpu_enter_suspend_mode();

	/* Use 48MHz clock to speed through wake-up */
	MEC1322_PCR_PROC_CLK_CTL = 1;

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
DECLARE_IRQ(MEC1322_IRQ_HTIMER, htimer_interrupt, 1);

enum ec_image system_get_shrspi_image_copy(void)
{
	return MEC1322_VBAT_RAM(MEC1322_IMAGETYPE_IDX);
}

uint32_t system_get_lfw_address(void)
{
	uint32_t *const lfw_vector =
		(uint32_t *const)CONFIG_PROGRAM_MEMORY_BASE;

	return *(lfw_vector + 1);
}

void system_set_image_copy(enum ec_image copy)
{
	MEC1322_VBAT_RAM(MEC1322_IMAGETYPE_IDX) =
		(copy == EC_IMAGE_RW) ? EC_IMAGE_RW : EC_IMAGE_RO;
}
