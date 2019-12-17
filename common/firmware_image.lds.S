/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "rwsig.h"

#ifdef NPCX_RO_HEADER
/*
 * Replace *_MEM_OFF with *_STORAGE_OFF to indicate flat file contains header
 * or some struture which doesn't belong to FW
 */
#define IMAGE_RO_AT (CONFIG_PROGRAM_MEMORY_BASE + \
		     CONFIG_EC_PROTECTED_STORAGE_OFF)
/* npcx uses *STORAGE_OFF to plan the layout of flash image */
#define IMAGE_RW_AT (CONFIG_PROGRAM_MEMORY_BASE + \
		     CONFIG_EC_WRITABLE_STORAGE_OFF + \
		     CONFIG_RW_STORAGE_OFF)
#define IMAGE_RW_B_AT (CONFIG_PROGRAM_MEMORY_BASE + \
		       CONFIG_EC_WRITABLE_STORAGE_OFF + \
		       CONFIG_RW_B_STORAGE_OFF)

#elif (CONFIG_RO_MEM_OFF == CONFIG_RW_MEM_OFF)
#define IMAGE_RO_AT (CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF)
/* This is applicable to ECs in which RO and RW execution is
mapped to the same location but we still have to generate an ec.bin with RO
and RW images at different Flash offset */
#define IMAGE_RW_AT (CONFIG_PROGRAM_MEMORY_BASE + \
		     CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define IMAGE_RW_B_AT (CONFIG_PROGRAM_MEMORY_BASE + \
		       CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE + CONFIG_RW_SIZE)

#else
#define IMAGE_RO_AT (CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF)
#define IMAGE_RW_AT (CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF)
#define IMAGE_RW_B_AT (CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_B_MEM_OFF)
#endif

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)

MEMORY
{
	FLASH (rx) : ORIGIN = CONFIG_PROGRAM_MEMORY_BASE,
		     LENGTH = CONFIG_FLASH_SIZE
#ifdef CONFIG_DRAM_BASE
	DRAM (rx) : ORIGIN = CONFIG_DRAM_BASE_LOAD, LENGTH = CONFIG_DRAM_SIZE
#endif
}

SECTIONS
{
	.image.RO : AT(IMAGE_RO_AT) {
		*(.image.RO)
	} > FLASH =0xff

#ifdef CONFIG_RWSIG_TYPE_RWSIG
	.image.RO.key : AT(CONFIG_RO_PUBKEY_ADDR) {
		*(.image.RO.key)
	} > FLASH =0xff
#endif

#ifdef CONFIG_ROLLBACK
	.image.ROLLBACK : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_ROLLBACK_OFF) {
		*(.image.ROLLBACK)
	} > FLASH =0xff
#endif

#ifdef CONFIG_SHAREDLIB
	.image.libsharedobjs : AT(CONFIG_PROGRAM_MEMORY_BASE + \
				  CONFIG_SHAREDLIB_MEM_OFF) {
		*(.image.libsharedobjs)
	} > FLASH =0xff
#endif

	.image.RW : AT(IMAGE_RW_AT) {
		*(.image.RW)
	} > FLASH =0xff
#ifdef CONFIG_RWSIG_TYPE_RWSIG
	.image.RW.sign : AT(CONFIG_RW_SIG_ADDR) {
		*(.image.RW.sign)
	} > FLASH =0xff
#endif
#ifdef CONFIG_RW_B_MEM_OFF
	.image.RW_B : AT(IMAGE_RW_B_AT) {
		*(.image.RW_B)
	} > FLASH =0xff
#ifdef CONFIG_RWSIG_TYPE_RWSIG
	.image.RW_B.sign : AT(CONFIG_RW_B_SIG_ADDR) {
		*(.image.RW_B.sign)
	} > FLASH =0xff
#endif
#endif
	.padding : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_FLASH_SIZE - 1) {
		BYTE(0xff);
	} > FLASH =0xff

#ifdef CONFIG_DRAM_BASE
	.image.RW.dram : AT(CONFIG_DRAM_BASE_LOAD) {
		*(.image.RW.dram)
	} > DRAM =0x00
#endif
}
