/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UARTN_DMA_H
#define __CROS_EC_UARTN_DMA_H

#include "common.h"

void uartn_dma_init(uint8_t uart_num);
void uartn_dma_rx_init(uint8_t uart_num);
void uartn_dma_start_rx(uint8_t uart_num, const void *memory, uint32_t count);
uint32_t uartn_dma_rx_bytes_done(uint8_t uart_num);
void uartn_dma_reset(uint8_t uart_num);

#endif /* __CROS_EC_UARTN_DMA_H */
