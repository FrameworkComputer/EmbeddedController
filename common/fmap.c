/*
 * Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>

#include "common.h"
#include "rwsig.h"
#include "util.h"
#include "version.h"

/*
 * FMAP structs.
 * See https://chromium.googlesource.com/chromiumos/third_party/flashmap/+/master/lib/fmap.h
 */
#define FMAP_NAMELEN 32
#define FMAP_SIGNATURE "__FMAP__"
#define FMAP_SIGNATURE_SIZE 8
#define FMAP_VER_MAJOR 1
#define FMAP_VER_MINOR 0

/*
 * For address containing CONFIG_PROGRAM_MEMORY_BASE (symbols in *.RO.lds.S and
 * variable), this computes the offset to the start of the image on flash.
 */
#define RELATIVE_RO(addr) ((addr) - CONFIG_PROGRAM_MEMORY_BASE - \
			   CONFIG_RO_MEM_OFF)

/*
 * All internal EC code assumes that offsets are provided relative to
 * physical address zero of storage. In some cases, the region of storage
 * belonging to the EC is not physical address zero - a non-zero fmap_base
 * indicates so. Since fmap_base is not yet handled correctly by external
 * code, we must perform the adjustment in our host command handlers -
 * adjust all offsets so they are relative to the beginning of the storage
 * region belonging to the EC. TODO(crbug.com/529365): Handle fmap_base
 * correctly in flashrom, dump_fmap, etc. and remove EC_FLASH_REGION_START.
 */
#if CONFIG_EC_WRITABLE_STORAGE_OFF < CONFIG_EC_PROTECTED_STORAGE_OFF
#define FMAP_REGION_START CONFIG_EC_WRITABLE_STORAGE_OFF
#else
#define FMAP_REGION_START CONFIG_EC_PROTECTED_STORAGE_OFF
#endif

struct fmap_header {
	char        fmap_signature[FMAP_SIGNATURE_SIZE];
	uint8_t     fmap_ver_major;
	uint8_t     fmap_ver_minor;
	uint64_t    fmap_base;
	uint32_t    fmap_size;
	char        fmap_name[FMAP_NAMELEN];
	uint16_t    fmap_nareas;
} __packed;

#define FMAP_AREA_STATIC      BIT(0)	/* can be checksummed */
#define FMAP_AREA_COMPRESSED  BIT(1)  /* may be compressed */
#define FMAP_AREA_RO          BIT(2)  /* writes may fail */

struct fmap_area_header {
	uint32_t area_offset;
	uint32_t area_size;
	char     area_name[FMAP_NAMELEN];
	uint16_t area_flags;
} __packed;

#ifdef CONFIG_RWSIG_TYPE_RWSIG
#define NUM_EC_FMAP_AREAS_RWSIG 2
#else
#define NUM_EC_FMAP_AREAS_RWSIG 0
#endif

#ifdef CONFIG_ROLLBACK
#define NUM_EC_FMAP_AREAS_ROLLBACK 1
#else
#define NUM_EC_FMAP_AREAS_ROLLBACK 0
#endif
#ifdef CONFIG_RW_B
#  ifdef CONFIG_RWSIG_TYPE_RWSIG
#    define NUM_EC_FMAP_AREAS_RW_B     2
#  else
#    define NUM_EC_FMAP_AREAS_RW_B     1
#  endif
#else
#define NUM_EC_FMAP_AREAS_RW_B     0
#endif

#define NUM_EC_FMAP_AREAS (7 + \
			NUM_EC_FMAP_AREAS_RWSIG + \
			NUM_EC_FMAP_AREAS_ROLLBACK + \
			NUM_EC_FMAP_AREAS_RW_B)

const struct _ec_fmap {
	struct fmap_header header;
	struct fmap_area_header area[NUM_EC_FMAP_AREAS];
} ec_fmap __keep __attribute__((section(".google"))) = {
	/* Header */
	{
		.fmap_signature = {'_', '_', 'F', 'M', 'A', 'P', '_', '_'},
		.fmap_ver_major = FMAP_VER_MAJOR,
		.fmap_ver_minor = FMAP_VER_MINOR,
		.fmap_base = CONFIG_PROGRAM_MEMORY_BASE,
		.fmap_size = CONFIG_FLASH_SIZE,
		/* Used to distinguish the EC FMAP from other FMAPs */
		.fmap_name = "EC_FMAP",
		.fmap_nareas = NUM_EC_FMAP_AREAS,
	},

	{
	/* RO Firmware */
		{
			/*
			 * Range of RO firmware to be updated. EC_RO
			 * section includes the bootloader section
			 * because it may need to be updated/paired
			 * with a different RO.  Verified in factory
			 * finalization by hash. Should not have
			 * volatile data (ex, calibration results).
			 */
			.area_name = "EC_RO",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START,
			.area_size = CONFIG_RO_SIZE + CONFIG_RO_STORAGE_OFF,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/* (Optional) RO firmware code. */
			.area_name = "FR_MAIN",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RO_STORAGE_OFF,
			.area_size = CONFIG_RO_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/*
			 * RO firmware version ID. Must be NULL terminated
			 * ASCII, and padded with \0.
			 */
			.area_name = "RO_FRID",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RO_STORAGE_OFF +
				RELATIVE_RO((uint32_t)__image_data_offset) +
				offsetof(struct image_data,  version),
			.area_size = sizeof(current_image_data.version),
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},

		/* Other RO stuff: FMAP, WP, KEYS, etc. */
		{
			.area_name = "FMAP",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RO_STORAGE_OFF +
				RELATIVE_RO((uint32_t)&ec_fmap),
			.area_size = sizeof(ec_fmap),
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/*
			 * The range for write protection, for factory
			 * finalization.  Should include (may be identical to)
			 * EC_RO and aligned to hardware specification.
			 */
			.area_name = "WP_RO",
			.area_offset = CONFIG_WP_STORAGE_OFF -
				       FMAP_REGION_START,
			.area_size = CONFIG_WP_STORAGE_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
#ifdef CONFIG_RWSIG_TYPE_RWSIG
		{
			/* RO public key address, for RW verification */
			.area_name = "KEY_RO",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RO_PUBKEY_ADDR -
				CONFIG_PROGRAM_MEMORY_BASE,
			.area_size = CONFIG_RO_PUBKEY_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
#endif

		/* RW Firmware */
		{
			 /* The range of RW firmware to be auto-updated. */
			.area_name = "EC_RW",
			.area_offset = CONFIG_EC_WRITABLE_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RW_STORAGE_OFF,
			.area_size = CONFIG_RW_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/*
			 * RW firmware version ID. Must be NULL terminated
			 * ASCII, and padded with \0.
			 * TODO: Get the relative offset of
			 * __image_data_offset within our RW image to
			 * accommodate image asymmetry.
			 */
			.area_name = "RW_FWID",
			.area_offset = CONFIG_EC_WRITABLE_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RW_STORAGE_OFF +
				RELATIVE_RO((uint32_t)__image_data_offset) +
				offsetof(struct image_data,  version),
			.area_size = sizeof(current_image_data.version),
			.area_flags = FMAP_AREA_STATIC,
		},
#ifdef CONFIG_ROLLBACK
		{
			/*
			 * RW rollback version, 32-bit unsigned integer.
			 * TODO: Get the relative offset of
			 * __image_data_offset within our RW image to
			 * accommodate image asymmetry.
			 */
			.area_name = "RW_RBVER",
			.area_offset = CONFIG_EC_WRITABLE_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RW_STORAGE_OFF +
				RELATIVE_RO((uint32_t)__image_data_offset) +
				offsetof(struct image_data, rollback_version),
			.area_size = sizeof(
				current_image_data.rollback_version),
			.area_flags = FMAP_AREA_STATIC,
		},
#endif
#ifdef CONFIG_RWSIG_TYPE_RWSIG
		{
			 /* RW image signature */
			.area_name = "SIG_RW",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RW_SIG_ADDR -
				CONFIG_PROGRAM_MEMORY_BASE,
			.area_size = CONFIG_RW_SIG_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
#endif
#ifdef CONFIG_RW_B
		/* RW Firmware */
		{
			 /* The range of RW firmware to be auto-updated. */
			.area_name = "EC_RW_B",
			.area_offset = CONFIG_EC_WRITABLE_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RW_STORAGE_OFF +
				CONFIG_RW_SIZE,
			.area_size = CONFIG_RW_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
#ifdef CONFIG_RWSIG_TYPE_RWSIG
		{
			 /* RW_B image signature */
			.area_name = "SIG_RW_B",
			.area_offset = CONFIG_EC_PROTECTED_STORAGE_OFF -
				FMAP_REGION_START + CONFIG_RW_B_SIG_ADDR -
				CONFIG_PROGRAM_MEMORY_BASE,
			.area_size = CONFIG_RW_SIG_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
#endif
#endif
	}
};
