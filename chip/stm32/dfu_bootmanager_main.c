/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * DFU Boot Manager Main for STM32
 *
 * When the Boot Manager Main is enabled, the RO application skips the
 * common runtime and setup. This reduces the flash size and avoids clock,
 * interrupt, and setup steps which conflict with the built in Boot Loaders
 * while minimizing the Flash Size.
 *
 * The Boot Manager Main will perform self checks of the Flash and backup
 * memory. Based on these results it will boot into the DFU or RW Application.
 */

#include "clock.h"
#include "dfu_bootmanager_shared.h"
#include "flash.h"
#include "registers.h"
#include "task.h"
#include "watchdog.h"

#ifdef CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT
#if CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT <= 0 || \
	CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT > DFU_BOOTMANAGER_VALUE_DFU
#error "Max reboot count is out of range"
#endif
#endif /* CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT */

/*
 * Checks if the RW region is valid by reading the first 8 bytes of flash, it
 * should not start with an erased block.
 *
 * The DFU boot manager should not jump into the RW region if it contains
 * invalid code as the EC is be unstable. A check will be performed to validate
 * the start of the RW region to verify that it contains valid data.
 * DFU programmers should erase this section of flash first and at this point,
 * the EC will no longer be able to jump into the RW application.
 *
 * The normal DFU programming sequence programming will work, but by
 * splitting into the following sequence we can protect against additional
 * failures.
 *
 * 1. Erase the first RW flash section. This will lock the EC out of RW.
 * 2. Update the remaining flash. Erase, program, and read back flash to
 *      to verify the operation was successful. Regions of the flash which
 *      are difficult to repair if an error occurs should be programmed next.
 * 3. Program the first RW flash section and exit DFU mode if verification is
 *      successful.
 *
 * @return 1 if erased, 0 if not erased
 */
static int rw_is_empty(void)
{
	if (IS_ENABLED(CONFIG_FLASH_CROS))
		return crec_flash_is_erased(CONFIG_RW_MEM_OFF, 8);
	/* No flash support, we cannot tell.  Assume not empty. */
	return 0;
}

/*
 * Reads the backup registers. This will trigger a jump to DFU if either
 * the application has requested it or if the reboot counter indicates
 * the device is likely in a bad state. A counter recording the number
 * of reboots will be incremented.
 *
 * @returns True if the backup memory region indicates we should boot into DFU.
 */
static bool backup_boot_checks(void)
{
	uint8_t value;

	if (dfu_bootmanager_backup_read(&value)) {
		/* Value stored is not valid, set it to a valid value. */
		dfu_bootmanager_backup_write(DFU_BOOTMANAGER_VALUE_CLEAR);
		return false;
	}
	if (value == DFU_BOOTMANAGER_VALUE_DFU)
		return true;
#ifdef CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT
	if (value >= CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT)
		return true;
	/* Increment the reboot loop counter. */
	value++;
	dfu_bootmanager_backup_write(value);
#endif /* CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT */
	return false;
}

/*
 * Performs the minimal set of initialization required for the boot manager.
 * The main application region or DFU boot loader have different prerequisites,
 * any configurations that are enabled either need to be benign with both
 * images or disabled prior to the jumps.
 */
static void dfu_bootmanager_init(void)
{
#ifdef CONFIG_WATCHDOG
	/*
	 * Enable the watchdog.  This boot stage will complete and jump to
	 * either RW or DFU bootloader so quickly, that there is not need to
	 * reset the watchdog counter.  Enabling it before jumping allows
	 * catching a bad RW, which may lock up in initialization before itself
	 * reaching the step of configuring the watchdog.  After such lockup is
	 * repeated CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT times, this RO stage
	 * will jump to DFU, allowing remote recovery.
	 */
	watchdog_init();
#endif
	/* enable clock on Power module */
#ifndef CHIP_FAMILY_STM32H7
#if defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32L5)
	STM32_RCC_APB1ENR1 |= STM32_RCC_PWREN;
#else
	STM32_RCC_APB1ENR |= STM32_RCC_PWREN;
#endif
#endif
#if defined(CHIP_FAMILY_STM32F4)
	/* enable backup registers */
	STM32_RCC_AHB1ENR |= STM32_RCC_AHB1ENR_BKPSRAMEN;
#elif defined(CHIP_FAMILY_STM32H7)
	/* enable backup registers */
	STM32_RCC_AHB4ENR |= BIT(28);
#elif defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32L5)
	/* enable RTC APB clock */
	STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_RTCAPBEN;
#else
	/* enable backup registers */
	STM32_RCC_APB1ENR |= BIT(27);
#endif
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
	/* Enable access to RCC CSR register and RTC backup registers */
	STM32_PWR_CR |= BIT(8);
}

/*
 * Load stack pointer and reset vector from an ARM vector table, and jump.
 */
static void jump_to_arm_reset_vector(uint32_t addr)
{
	/*
	 * The first 32-bit entry ARM vector table is the initial value of the
	 * stack pointer, the second entry is the reset vector.
	 */
	asm("mov r1, %0\n"
	    /* Load stack pointer */
	    "ldr r0, [r1, 0]\n"
	    "msr msp, r0\n"
	    /*
	     * Memory barrier to ensure subsequent instructions uses modified
	     * stack pointer.
	     */
	    "isb\n"
	    /* Load reset vector */
	    "ldr r0, [r1, 4]\n"
	    /* Jump without saving return address (would modify msp) */
	    "bx r0\n"
	    : /* no outputs */
	    : "r"(addr)
	    :);
}

static void jump_to_rw(void)
{
	jump_to_arm_reset_vector(CONFIG_PROGRAM_MEMORY_BASE +
				 CONFIG_RW_MEM_OFF);
}

static void jump_to_dfu(void)
{
	/* Clear the scratchpad. */
	dfu_bootmanager_backup_write(DFU_BOOTMANAGER_VALUE_CLEAR);

	jump_to_arm_reset_vector(STM32_DFU_BASE);
}

/*
 * DFU Boot Manager main. It'll check if the RW region is not fully programmed
 * or if the backup memory indicates we should reboot into DFU.
 */
int main(void)
{
	dfu_bootmanager_init();

	if (rw_is_empty() || backup_boot_checks())
		jump_to_dfu();
	jump_to_rw();

	return 0;
}

/*
 * The RW application will replace the vector table and exception handlers
 * shortly after the jump. If the application is corrupt and fails before
 * this, the only action that can be done is jumping into DFU mode.
 */
void __keep exception_panic(void)
{
	dfu_bootmanager_enter_dfu();
}

/*
 * Function stubs which are required by bkpdata.c and system.c:
 * Interrupts are always disabled in the Boot Manager so we do not
 * need to worry about concurrent access.
 */

void task_clear_pending_irq(int irq)
{
}
void interrupt_disable(void)
{
}
void mutex_lock(mutex_t *mtx)
{
}
void mutex_unlock(mutex_t *mtx)
{
}

bool in_interrupt_context(void)
{
	return false;
}
