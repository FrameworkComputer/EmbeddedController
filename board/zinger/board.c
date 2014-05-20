/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Tiny charger configuration */

#include "common.h"
#include "debug.h"
#include "registers.h"
#include "sha1.h"
#include "task.h"
#include "usb_pd.h"
#include "util.h"

extern void pd_rx_handler(void);

/* RW firmware reset vector */
static uint32_t * const rw_rst =
	(uint32_t *)(CONFIG_FLASH_BASE+CONFIG_FW_RW_OFF+4);

/* External interrupt EXTINT7 for external comparator on PA7 */
void pd_rx_interrupt(void)
{
	/* trigger reception handling */
	pd_rx_handler();
}
DECLARE_IRQ(STM32_IRQ_EXTI4_15, pd_rx_interrupt, 1);

static void jump_to_rw(void)
{
	void (*jump_rw_rst)(void) = (void *)*rw_rst;

	debug_printf("Jump to RW\n");
	/* Disable interrupts */
	asm volatile("cpsid i");
	/* Call RW firmware reset vector */
	jump_rw_rst();
}

int is_ro_mode(void)
{
	return (uint32_t)&jump_to_rw < (uint32_t)rw_rst;
}

static int check_rw_valid(void)
{
	uint32_t *hash;
	uint32_t *fw_hash = (uint32_t *)
		(CONFIG_FLASH_BASE + CONFIG_FLASH_SIZE - 32);

	/* Check if we have a RW firmware flashed */
	if (*rw_rst == 0xffffffff)
		return 0;

	hash = (uint32_t *)flash_hash_rw();
	/* TODO(crosbug.com/p/28336) use secret key to check RW */
	if (memcmp(hash, fw_hash, SHA1_DIGEST_SIZE) != 0) {
		/* Firmware doesn't match the recorded hash */
		debug_printf("SHA-1 %08x %08x %08x %08x %08x\n",
			hash[0], hash[1], hash[2], hash[3], hash[4]);
		debug_printf("FW SHA-1 %08x %08x %08x %08x %08x\n",
			fw_hash[0], fw_hash[1], fw_hash[2],
			fw_hash[3], fw_hash[4]);
		return 0;
	}

	return 1;
}

extern void pd_task(void);

int main(void)
{
	hardware_init();
	debug_printf("Power supply started ... %s\n",
		is_ro_mode() ? "RO" : "RW");

	/* Verify RW firmware and use it if valid */
	if (is_ro_mode() && check_rw_valid())
		jump_to_rw();

	/* background loop for PD events */
	pd_task();

	debug_printf("background loop exited !\n");
	/* we should never reach that point */
	cpu_reset();
	return 0;
}
