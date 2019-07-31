/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_CHIP_G_FLASH_INFO_H
#define __EC_CHIP_G_FLASH_INFO_H

#include <stddef.h>

#include "signed_header.h"

/*
 * Info1 space available to the app firmware is split in four equal size
 * areas, used as follows:
 *
 * Area 0 - RO rollback prevention
 * Area 1 - RW rollback prevention
 * Area 2 - Board specific stuff
 * Area 3 - Crypto scratch
 */
#define INFO_AREA_SIZE	   (INFO_MAX * 4)
#define INFO_TOTAL_SIZE	   (INFO_AREA_SIZE * 4)

#define INFO_RO_MAP_OFFSET 0
#define INFO_RO_MAP_SIZE   INFO_AREA_SIZE

#define INFO_RW_MAP_OFFSET (INFO_RO_MAP_OFFSET + INFO_RO_MAP_SIZE)
#define INFO_RW_MAP_SIZE   INFO_AREA_SIZE

#define INFO_BOARD_SPACE_OFFSET (INFO_RW_MAP_OFFSET + INFO_RW_MAP_SIZE)

/* This in fact enables both read and write. */
void flash_info_write_enable(void);
void flash_info_write_disable(void);
int flash_info_physical_write(int byte_offset, int num_bytes, const char *data);
int flash_physical_info_read_word(int byte_offset, uint32_t *dst);

void flash_open_ro_window(uint32_t offset, size_t size_b);

#endif  /* ! __EC_CHIP_G_FLASH_INFO_H */
