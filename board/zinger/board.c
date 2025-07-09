/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Tiny charger configuration */

#include "common.h"
#include "cros_version.h"
#include "debug_printf.h"
#include "ec_commands.h"
#include "registers.h"
#include "rsa.h"
#include "rwsig.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "usb_pd.h"
#include "util.h"

/* Large 768-Byte buffer for RSA computation : could be re-use afterwards... */
static uint32_t rsa_workbuf[3 * RSANUMWORDS];

/* RW firmware reset vector */
static uint32_t *const rw_rst =
	(uint32_t *)(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF + 4);

/* External interrupt EXTINT7 for external comparator on PA7 */
static void pd_rx_interrupt(void)
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
	interrupt_disable();
	/* Call RW firmware reset vector */
	jump_rw_rst();
}

int is_ro_mode(void)
{
	return (uint32_t)&jump_to_rw < (uint32_t)rw_rst;
}

static int check_rw_valid(void *rw_hash)
{
	int good;

	/* Check if we have a RW firmware flashed */
	if (*rw_rst == 0xffffffff)
		return 0;

	good = rsa_verify(
		(const struct rsa_public_key *)(CONFIG_MAPPED_STORAGE_BASE +
						CONFIG_RO_PUBKEY_OFF),
		(const uint8_t *)(CONFIG_MAPPED_STORAGE_BASE +
				  CONFIG_RW_SIG_OFF),
		rw_hash, rsa_workbuf);
	if (!good) {
		debug_printf("RSA FAILED\n");
		pd_log_event(PD_EVENT_ACC_RW_FAIL, 0, 0, NULL);
		return 0;
	}

	return 1;
}

extern void pd_task(void *u);

int main(void)
{
	void *rw_hash;

	hardware_init();
	debug_printf("%s started\n", is_ro_mode() ? "RO" : "RW");

	/* the RO partition protection is not enabled : do it */
	if (!flash_physical_is_permanently_protected())
		flash_physical_permanent_protect();

	/*
	 * calculate the hash of the RW partition
	 *
	 * Also pre-cache it so we can answer Discover Identity VDM
	 * fast enough (in less than 30ms).
	 */
	rw_hash = flash_hash_rw();

	/* Verify RW firmware and use it if valid */
	if (is_ro_mode() && check_rw_valid(rw_hash))
		jump_to_rw();

	/* background loop for PD events */
	pd_task(NULL);

	debug_printf("EXIT!\n");
	/* we should never reach that point */
	system_reset(0);
	return 0;
}
