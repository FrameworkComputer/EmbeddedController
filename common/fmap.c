/*
 * Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>

#include "board.h"
#include "common.h"
#include "config.h"
#include "version.h"

/* FMAP structs. See http://code.google.com/p/flashmap/wiki/FmapSpec */
#define FMAP_NAMELEN 32
#define FMAP_SIGNATURE "__FMAP__"
#define FMAP_SIGNATURE_SIZE 8
#define FMAP_VER_MAJOR 1
#define FMAP_VER_MINOR 0

/*
 * For address containing CONFIG_FLASH_BASE (symbols in *.lds.S and variable),
 * this computes the offset to the start of flash.
 */
#define RELATIVE(addr) ((addr) - CONFIG_FLASH_BASE)

typedef struct _FmapHeader {
	char        fmap_signature[FMAP_SIGNATURE_SIZE];
	uint8_t     fmap_ver_major;
	uint8_t     fmap_ver_minor;
	uint64_t    fmap_base;
	uint32_t    fmap_size;
	char        fmap_name[FMAP_NAMELEN];
	uint16_t    fmap_nareas;
} __packed FmapHeader;

#define FMAP_AREA_STATIC      (1 << 0)	/* can be checksummed */
#define FMAP_AREA_COMPRESSED  (1 << 1)  /* may be compressed */
#define FMAP_AREA_RO          (1 << 2)  /* writes may fail */

typedef struct _FmapAreaHeader {
	uint32_t area_offset;
	uint32_t area_size;
	char     area_name[FMAP_NAMELEN];
	uint16_t area_flags;
} __packed FmapAreaHeader;

#define NUM_EC_FMAP_AREAS 7

const struct _ec_fmap {
	FmapHeader header;
	FmapAreaHeader area[NUM_EC_FMAP_AREAS];
} ec_fmap __attribute__((section(".google"))) = {
	/* Header */
	{
		.fmap_signature = {'_', '_', 'F', 'M', 'A', 'P', '_', '_'},
		.fmap_ver_major = FMAP_VER_MAJOR,
		.fmap_ver_minor = FMAP_VER_MINOR,
		.fmap_base = CONFIG_FLASH_BASE,
		.fmap_size = CONFIG_FLASH_SIZE,
		.fmap_name = "EC_FMAP",
		.fmap_nareas = NUM_EC_FMAP_AREAS,
	},

	{
	/* RO Firmware */
		{
			/* Range of RO firmware to be updated. Verified in
			 * factory finalization by hash. Should not have
			 * volatile data (ex, calibration results). */
			.area_name = "EC_RO",
			.area_offset = CONFIG_SECTION_RO_OFF,
			.area_size = CONFIG_SECTION_RO_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/* (Optional) RO firmware code. */
			.area_name = "FR_MAIN",
			.area_offset = CONFIG_FW_RO_OFF,
			.area_size = CONFIG_FW_RO_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/* RO firmware version ID. Must be NULL terminated
			 * ASCIIZ, and padded with \0. */
			.area_name = "RO_FRID",
			.area_offset = CONFIG_FW_RO_OFF +
				RELATIVE((uint32_t)__version_struct_offset) +
				offsetof(struct version_struct,  version),
			.area_size = sizeof(version_data.version),
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},

		/* Other RO stuff: FMAP, WP, KEYS, etc. */
		{
			.area_name = "FMAP",
			.area_offset = CONFIG_FW_RO_OFF +
				RELATIVE((uint32_t)&ec_fmap),
			.area_size = sizeof(ec_fmap),
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/* The range for write protection, for factory
			 * finalization.  Should include (or identical to)
			 * EC_RO and aligned to hardware specification. */
			.area_name = "WP_RO",
			.area_offset = CONFIG_SECTION_RO_OFF,
			.area_size = CONFIG_SECTION_RO_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},

		/* RW Firmware */
		{
			 /* The range of RW firmware to be auto-updated. */
			.area_name = "EC_RW",
			.area_offset = CONFIG_SECTION_RW_OFF,
			.area_size = CONFIG_SECTION_RW_SIZE,
			.area_flags = FMAP_AREA_STATIC | FMAP_AREA_RO,
		},
		{
			/* RW firmware version ID. Must be NULL terminated
			 * ASCIIZ, and padded with \0. */
			.area_name = "RW_FWID",
			.area_offset = CONFIG_FW_RW_OFF +
				RELATIVE((uint32_t)__version_struct_offset) +
				offsetof(struct version_struct,  version),
			.area_size = sizeof(version_data.version),
			.area_flags = FMAP_AREA_STATIC,
		},
	}
};
