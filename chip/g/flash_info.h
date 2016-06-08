/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_CHIP_G_FLASH_INFO_H
#define __EC_CHIP_G_FLASH_INFO_H

void flash_info_write_enable(void);
void flash_info_write_disable(void);
int flash_info_physical_write(int byte_offset, int num_bytes, const char *data);

#endif  /* ! __EC_CHIP_G_FLASH_INFO_H */
