/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_KEYBORG_SPI_COMM_H
#define __BOARD_KEYBORG_SPI_COMM_H

#define SPI_PACKET_MAX_SIZE 64

enum ts_command {
	TS_CMD_HELLO = 0,
};

struct spi_comm_packet {
	uint8_t size;
	uint8_t cmd_sts;
	uint8_t data[0];
};

#define SPI_PACKET_HEADER_SIZE 2

/* Initialize SPI interface for the master chip */
void spi_master_init(void);

/* Initialize SPI interface for the slave chip */
void spi_slave_init(void);

/*
 * Calculate checksum and send command packet to the slave.
 *
 * @param cmd		Pointer to the command packet.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int spi_master_send_command(struct spi_comm_packet *cmd);

/*
 * Wait for slave response and verify checksum.
 *
 * @return		Pointer to the response packet, or NULL if any error.
 */
const struct spi_comm_packet *spi_master_wait_response(void);

/*
 * Start receiving slave response, but don't wait for full transaction.
 * The caller is responsible for calling spi_master_wait_response_done()
 * to ensure the response is fully received.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int spi_master_wait_response_async(void);

/*
 * Wait for slave response to complete.
 *
 * @return		Pointer to the response packet, or NULL if any error.
 */
const struct spi_comm_packet *spi_master_wait_response_done(void);

/*
 * Calculate checksum and send response packet to the master.
 *
 * @param resp		Pointer to the response packet.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int spi_slave_send_response(struct spi_comm_packet *resp);

/*
 * Start sending response to the master, but don't block. The caller is
 * responsible for calling spi_slave_send_response_flush() to ensure
 * the response is fully transmitted.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int spi_slave_send_response_async(struct spi_comm_packet *resp);

/*
 * Wait until the last response is sent out.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int spi_slave_send_response_flush(void);

/*
 * Perform random back-to-back hello test. Master only.
 *
 * @param iteration	Number of hello messages to send.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int spi_hello_test(int iteration);

#endif /* __BOARD_KEYBORG_SPI_COMM_H */
