/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"
#include "debug_printf.h"
#include "key_ladder.h"
#include "registers.h"
#include "rom_flash.h"
#include "setup.h"
#include "signed_header.h"
#include "uart.h"
#include "verify.h"

static int unlockedForExecution(void)
{
	return GREAD_FIELD(GLOBALSEC, SB_COMP_STATUS, SB_BL_SIG_MATCH);
}

void _jump_to_address(const void *addr)
{
	REG32(GC_M3_VTOR_ADDR) = (unsigned)addr;  /* Set vector base. */

	__asm__ volatile("ldr sp, [%0]; \
			ldr pc, [%0, #4];"
			 : : "r"(addr)
			 : "memory");

	__builtin_unreachable();
}

void tryLaunch(uint32_t adr, size_t max_size)
{
	static struct {
		uint32_t img_hash[SHA256_DIGEST_WORDS];
		uint32_t fuses_hash[SHA256_DIGEST_WORDS];
		uint32_t info_hash[SHA256_DIGEST_WORDS];
	} hashes;
	static uint32_t hash[SHA256_DIGEST_WORDS];
	static uint32_t fuses[FUSE_MAX];
	static uint32_t info[INFO_MAX];
	int i;
	uint32_t major;
	const uint32_t FAKE_rom_hash[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	const struct SignedHeader *hdr = (const struct SignedHeader *)(adr);

	memset(&hashes, 0, sizeof(hashes));

	/* Sanity check image header. */
	if (hdr->magic != -1)
		return;
	if (hdr->image_size > max_size)
		return;

	/* Sanity checks that image belongs at adr. */
	if (hdr->ro_base < adr)
		return;
	if (hdr->ro_max > adr + max_size)
		return;
	if (hdr->rx_base < adr)
		return;
	if (hdr->rx_max > adr + max_size)
		return;

	VERBOSE("considering image at 0x%8x\n", hdr);
	VERBOSE("image size 0x%8x\n", hdr->image_size);
	VERBOSE("hashing from 0x%8x to 0x%8x\n",
		&hdr->tag, adr + hdr->image_size);

	/* Setup candidate execution region 1 based on header information. */
	/* TODO: harden against glitching: multi readback, check? */
	GREG32(GLOBALSEC, CPU0_I_STAGING_REGION1_BASE_ADDR) = hdr->rx_base;
	GREG32(GLOBALSEC, CPU0_I_STAGING_REGION1_SIZE) =
		hdr->rx_max - hdr->rx_base - 1;
	GWRITE_FIELD(GLOBALSEC, CPU0_I_STAGING_REGION1_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, CPU0_I_STAGING_REGION1_CTRL, RD_EN, 1);
	DCRYPTO_SHA256_hash((uint8_t *) &hdr->tag,
			hdr->image_size - offsetof(struct SignedHeader, tag),
			(uint8_t *) hashes.img_hash);

	VERBOSE("img_hash  : %.32h\n", hashes.img_hash);

	/* Sense fuses into RAM array; hash array. */
	/* TODO: is this glitch resistant enough? Certainly is simple.. */
	for (i = 0; i < FUSE_MAX; ++i)
		fuses[i] = FUSE_IGNORE;

	for (i = 0; i < FUSE_MAX; ++i) {
		/*
		 * For the fuses the header cares about, read their values
		 * into the map.
		 */
		if (hdr->fusemap[i>>5] & (1 << (i&31))) {
			/*
			 * BNK0_INTG_CHKSUM is the first fuse and as such the
			 * best reference to the base address of the fuse
			 * memory map.
			 */
			fuses[i] = GREG32_ADDR(FUSE, BNK0_INTG_CHKSUM)[i];
		}
	}

	DCRYPTO_SHA256_hash((uint8_t *) fuses, sizeof(fuses),
			(uint8_t *) hashes.fuses_hash);

	VERBOSE("fuses_hash: %.32h\n", hashes.fuses_hash);

	/* Sense info into RAM array; hash array. */
	for (i = 0; i < INFO_MAX; ++i)
		info[i] = INFO_IGNORE;

	for (i = 0; i < INFO_MAX; ++i) {
		if (hdr->infomap[i>>5] & (1 << (i&31))) {
			uint32_t val = 0;
			/* read 2nd bank of info */
			int retval = flash_info_read(i + INFO_MAX, &val);

			info[i] ^= val ^ retval;
		}
	}

	DCRYPTO_SHA256_hash((uint8_t *) info, sizeof(info),
			(uint8_t *) hashes.info_hash);
	VERBOSE("info_hash : %.32h\n", hashes.info_hash);

	/* Hash our set of hashes to get final hash. */
	DCRYPTO_SHA256_hash((uint8_t *) &hashes, sizeof(hashes),
			(uint8_t *) hash);

	/*
	 * Write measured hash to unlock register to try and unlock execution.
	 * This would match when doing warm-boot from suspend, so we can avoid
	 * the slow RSA verify.
	 */
	for (i = 0; i < SHA256_DIGEST_WORDS; ++i)
		GREG32_ADDR(GLOBALSEC, SB_BL_SIG0)[i] = hash[i];

	/*
	 * Unlock attempt. Value written is irrelevant, as long as something
	 * is written.
	 */
	GREG32(GLOBALSEC, SIG_UNLOCK) = 1;

	if (!unlockedForExecution()) {
		/* Assume warm-boot failed; do full RSA verify. */
		LOADERKEY_verify(&hdr->keyid, hdr->signature, hash);
		/*
		 * PWRDN_SCRATCH* should be write-locked, tied to successful
		 * SIG_MATCH. Thus ARM is only able to write this hash if
		 * signature was correct.
		 */
		for (i = 0; i < SHA256_DIGEST_WORDS; ++i)
			/* TODO: verify written values as glitch protection? */
			GREG32_ADDR(PMU, PWRDN_SCRATCH8)[i] = hash[i];
	}


	if (!unlockedForExecution()) {
		debug_printf("Failed to unlock for execution image at 0x%08x\n",
			     adr);
		return;
	}

	/*
	 * Write PMU_PWRDN_SCRATCH_LOCK1_OFFSET to lock against rewrites.
	 * TODO: glitch resist
	 */
	GREG32(PMU, PWRDN_SCRATCH_LOCK1) = 1;

	/*
	 * Drop software level to stop SIG_MATCH from future write-unlocks.
	 * TODO: glitch detect / verify?
	 */
	GREG32(GLOBALSEC, SOFTWARE_LVL) = 0x33;

	/* Write hdr->tag, hdr->epoch_ to KDF engine FWR[0..7] */
	for (i = 0; i < ARRAY_SIZE(hdr->tag); ++i)
		GREG32_ADDR(KEYMGR, HKEY_FWR0)[i] = hdr->tag[i];

	GREG32(KEYMGR, HKEY_FWR7) = hdr->epoch_;

	/* Crank keyladder */
	if (!(GREAD(FUSE, FLASH_PERSO_PAGE_LOCK) &
	      (GC_FUSE_HIK_CREATE_LOCK_VAL_MASK <<
	       GC_FUSE_HIK_CREATE_LOCK_VAL_LSB))) {
		VERBOSE("Re-reading INFO0\n");
		/*
		 * Needed because FUSE_FLASH_PERSO_PAGE_LOCK_OFFSET isn't
		 * blown) wipe out the flash secrets saved in keymgr and
		 * re-read info0
		 */
		GREG32(KEYMGR, FLASH_RCV_WIPE) = 1;
		GREG32(FLASH, FSH_ENABLE_INFO0_SHADOW_READ) = 1;
	}

	/* Turn up random stalls for SHA */
	GREG32(KEYMGR, SHA_RAND_STALL_CTL_FREQ) = 0;  /* 0:50% */

	major = hdr->major_;
	GREG32(KEYMGR, FW_MAJOR_VERSION) = major;

	/*
	 * Lock FWR (NOTE: needs to happen after writing major!) TODO: glitch
	 * protect?
	 */
	GREG32(KEYMGR, FWR_VLD) = 2;
	GREG32(KEYMGR, FWR_LOCK) = 1;

	key_ladder_step(40, FAKE_rom_hash);

	/* TODO: do cert #40 and lock in ROM? */
	GREG32(GLOBALSEC, HIDE_ROM) = 1;

	/* TODO: bump runlevel(s) according to signature header */
	/*
	 * Flash write protect entire image area (to guard signed blob)
	 * REGION0 protects boot_loader, use REGION1 to protect app
	 */
	GREG32(GLOBALSEC, FLASH_REGION1_BASE_ADDR) = adr;
	GREG32(GLOBALSEC, FLASH_REGION1_SIZE) = hdr->image_size - 1;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION1_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION1_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION1_CTRL, WR_EN, 0);

	/* TODO: lock FLASH_REGION 1? */
	disarmRAMGuards();

	debug_printf("Valid image found at 0x%08x, jumping", hdr);
	uart_tx_flush();

	_jump_to_address(&hdr[1]);
}
