/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INCLUDE_SPS_H
#define __CROS_EC_INCLUDE_SPS_H

#include "spi.h"
#include "util.h"

/* SPS Control Mode */
enum sps_mode {
	SPS_GENERIC_MODE = 0,
	SPS_SWETLAND_MODE = 1,
	SPS_ROM_MODE = 2,
	SPS_UNDEF_MODE = 3,
};

/* Receive and transmit FIFO size and mask. */
#define SPS_FIFO_SIZE		BIT(10)
#define SPS_FIFO_MASK		(SPS_FIFO_SIZE - 1)

/*
 * Tx interrupt callback function prototype. This function returns a portion
 * of the received SPI data and current status of the CS line. When CS is
 * deasserted, this function is called with data_size of zero and a non-zero
 * cs_status. This allows the recipient to delineate the SPS frames.
 */
typedef void (*rx_handler_f)(uint8_t *data, size_t data_size, int cs_disabled);

/*
 * Push data to the SPS TX FIFO
 * @param data Pointer to 8-bit data
 * @param data_size Number of bytes to transmit
 * @return : actual number of bytes placed into tx fifo
 */
int sps_transmit(uint8_t *data, size_t data_size);

/*
 * These functions return zero on success or non-zero on failure (attempt to
 * register a callback on top of existing one, or attempt to unregister
 * non-existing callback.
 *
 * rx_fifo_threshold value of zero means 'default'.
 */
int sps_register_rx_handler(enum sps_mode mode,
			    rx_handler_f rx_handler,
			    unsigned rx_fifo_threshold);
int sps_unregister_rx_handler(void);
void sps_tx_status(uint8_t byte);

#endif
