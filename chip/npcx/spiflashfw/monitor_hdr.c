/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX SoC spi flash update tool -  monitor firmware header
 */

#include "config.h"
#include "npcx_monitor.h"

const struct monitor_header_tag monitor_hdr = {
	/* 0x00: TAG = 0xA5075001 */
	NPCX_MONITOR_UUT_TAG,
	/* 0x04: Size·of·the·EC image·be·programmed.
	 * Default = code RAM size
	 */
	NPCX_PROGRAM_MEMORY_SIZE,
	/*
	 * 0x08: The start of RAM address to store the EC image, which will be
	 * programed into the SPI flash.
	 */
	CONFIG_PROGRAM_MEMORY_BASE,
	/* 0x0C:The Flash start address to be programmed*/
#ifdef SECTION_IS_RO
	/* Default: RO image is programed from the start of SPI flash */
	CONFIG_EC_PROTECTED_STORAGE_OFF,
#else
	/* Default: RW image is programed from the half of SPI flash */
	CONFIG_EC_WRITABLE_STORAGE_OFF,
#endif
	/* 0x10: Maximum allowable flash clock frequency */
	0,
	/* 0x11: SPI Flash read mode */
	0,
	/* 0x12: Reserved */
	0,
};
