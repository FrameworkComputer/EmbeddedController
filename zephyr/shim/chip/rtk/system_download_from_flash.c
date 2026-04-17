/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "reg/reg_wdt.h"
#include "soc.h"
#include "system_chip.h"

#include <inttypes.h>

#include <zephyr/sys/__assert.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys_clock.h>
#include <zephyr/toolchain.h>

#include <timer.h>

#define WDT_NODE DT_INST(0, realtek_rts5912_watchdog)
#define RTK_WDT_REG_BASE ((WDT_Type *)(DT_REG_ADDR(WDT_NODE)))

typedef void (*start_lfw_func)(uint32_t, uint32_t, uint32_t, uint32_t);

FUNC_NORETURN void __keep __attribute__((section(".code_in_sram2")))
__start_lfw(uint32_t srcAddr, uint32_t dstAddr, uint32_t size, uint32_t exeAddr)
{
	uint8_t *flash_ptr = (uint8_t *)(CONFIG_MAPPED_STORAGE_BASE + srcAddr);
	uint8_t *sram_ptr = (uint8_t *)dstAddr;
	uint32_t i;

	/* Stop the watchdog */
	RTK_WDT_REG_BASE->INTEN = 0U;
	RTK_WDT_REG_BASE->CTRL |= WDT_CTRL_CLRRSTFLAG;
	RTK_WDT_REG_BASE->CTRL &= ~WDT_CTRL_EN;

	for (i = 0; i < size; i++) {
		sram_ptr[i] = flash_ptr[i];
	}

	__asm volatile("blx %0" ::"r"(*(uint32_t *)(exeAddr)) :);

	/* Should never get here */
	while (1)
		;
}

uintptr_t __lfw_sram_start = 0x2004fd00u;

void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
				uint32_t size, uint32_t exeAddr)
{
	uint32_t i;
	start_lfw_func __start_lfw_in_sram =
		(start_lfw_func)(__lfw_sram_start | 0x01);

	__ASSERT_NO_MSG((size % 4) == 0 && (srcAddr % 4) == 0 &&
			(dstAddr % 4) == 0);
	__ASSERT_NO_MSG(exeAddr != 0x0);

	/* Copy the lfw to help load fw */
	for (i = 0; i < &__flash_lplfw_end - &__flash_lplfw_start; i++) {
		*((uint32_t *)__lfw_sram_start + i) =
			*(&__flash_lplfw_start + i);
	}

	__start_lfw_in_sram(srcAddr, dstAddr, size, exeAddr);

	while (1)
		;
}
