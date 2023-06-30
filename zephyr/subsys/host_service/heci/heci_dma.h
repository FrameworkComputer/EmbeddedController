/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci_internal.h"

#define GET_NUM_PAGE_BITMAPS(size) ((size + BITS_PER_DW - 1) / BITS_PER_DW)
#define GET_NUM_PAGES(size) \
	((size + CONFIG_HECI_PAGE_SIZE - 1) / CONFIG_HECI_PAGE_SIZE)
#define BITMAP_SLC(idx) ((idx) / BITS_PER_DW)
#define BITMAP_BIT(idx) ((idx) % BITS_PER_DW)

#define GET_MSB(data64) ((uint32_t)((data64) >> 32))
#define GET_LSB(data64) ((uint32_t)(data64))

#define DMA_TIMEOUT_MS 5000

extern struct heci_device_t heci_dev;
extern struct k_mutex dev_lock;

bool send_client_msg_dma(struct heci_conn_t *conn, struct mrd_t *msg);

void heci_dma_alloc_notification(struct heci_bus_msg_t *msg);

void heci_dma_xfer_ack(struct heci_bus_msg_t *msg);
