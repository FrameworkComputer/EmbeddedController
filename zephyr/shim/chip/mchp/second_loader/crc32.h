/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stdlib.h>

uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t data_len);
uint32_t crc32_init(void);
uint32_t crc32_finalize(uint32_t crc);

#endif /* #ifndef CRC32_H */
