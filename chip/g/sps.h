/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_SPS_H
#define __CROS_EC_SPS_H

/*
 * API for the Cr50 SPS (SPI slave) controller. The controller deploys a 2KB
 * buffer split evenly between receive and transmit directions.
 *
 * Each one kilobyte of memory is organized into a FIFO with read and write
 * pointers. RX FIFO write and TX FIFO read pointers are managed by hardware.
 * RX FIFO read and TX FIFO write pointers are managed by software.
 */

#include <stdint.h>
#include <stddef.h>
#include "spi.h"

/* SPS Control Mode */
enum sps_mode {
	SPS_GENERIC_MODE = 0,
	SPS_SWETLAND_MODE = 1,
	SPS_ROM_MODE = 2,
	SPS_UNDEF_MODE = 3,
};

/**
 * Every RX byte simultaneously sends a TX byte, no matter what. This
 * specifies the TX byte to send when there's no data in the TX FIFO.
 *
 * @param byte	 dummy byte to send (default is 0xFF)
 */
void sps_tx_status(uint8_t byte);

/**
 * Add data to the SPS TX FIFO
 *
 * @param data        Pointer to 8-bit data
 * @param data_size   Number of bytes to transmit
 * @return            Number of bytes placed into the TX FIFO
 */
int sps_transmit(uint8_t *data, size_t data_size);

/**
 * The RX handler function is called in interrupt context to process incoming
 * bytes. It is passed a pointer to the linear space in the RX FIFO and the
 * number of bytes available at that address.
 *
 * If the RX FIFO wraps around, the RX FIFO handler may be called twice during
 * one interrupt.
 *
 * The handler is also called when the chip select deasserts, in case any
 * cleanup is required.
 *
 * @param data        Pointer to the incoming data, in its buffer
 * @param data_size   Number of new bytes visible without wrapping
 * @param cs_enabled  True if the chip select is still enabled
 */
typedef void (*rx_handler_fn)(uint8_t *data, size_t data_size, int cs_enabled);

/**
 * Register the RX handler function. This will reset and disable the RX FIFO,
 * replace any previous handler, then enable the RX FIFO.
 *
 * @param m_spi       SPI clock polarity and phase
 * @param m_sps       SPS interface protocol
 * @param func        RX handler function
 */
void sps_register_rx_handler(enum spi_clock_mode m_spi,
			     enum sps_mode m_sps,
			     rx_handler_fn func);

/**
 * Unregister the RX handler. This will reset and disable the RX FIFO.
 */
void sps_unregister_rx_handler(void);


/* Statistics counters, present only with CONFIG_SPS_TEST. */
extern uint32_t sps_tx_count, sps_rx_count,
	sps_tx_empty_count, sps_max_rx_batch;

#endif	/* __CROS_EC_SPS_H */
