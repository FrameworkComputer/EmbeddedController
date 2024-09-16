/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "spi.h"

int periphery_spi_write_read(uint8_t *tx_addr, uint32_t tx_len, uint8_t *rx_buf,
			     uint32_t rx_len);