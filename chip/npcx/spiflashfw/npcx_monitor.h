/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_NPCX_MONITOR_H
#define __CROS_EC_NPCX_MONITOR_H

#include <stdint.h>

#define NPCX_MONITOR_UUT_TAG      0xA5075001
#define NPCX_MONITOR_HEADER_ADDR  0x200C3000

/* Flag to record the progress of programming SPI flash */
#define SPI_PROGRAMMING_FLAG     0x200C4000

struct monitor_header_tag {
	/* offset 0x00: TAG NPCX_MONITOR_TAG */
	uint32_t tag;
	/* offset 0x04: Size of the binary being programmed (in bytes) */
	uint32_t size;
	/* offset 0x08: The RAM address of the binary to program into the SPI */
	uint32_t src_addr;
	/* offset 0x0C: The Flash address to be programmed (Absolute address) */
	uint32_t dest_addr;
	/* offset 0x10: Maximum allowable flash clock frequency */
	uint8_t  max_clock;
	/* offset 0x11: SPI Flash read mode */
	uint8_t  read_mode;
	/* offset 0x12: Reserved */
	uint16_t reserved;
} __packed;

#endif /* __CROS_EC_NPCX_MONITOR_H */
