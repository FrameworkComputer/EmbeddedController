/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file smbus.h
 * @brief smbus interface APIs
 * @see http://smbus.org/specs/smbus20.pdf
 */
#ifndef __EC_SMBUS_H__
#define __EC_SMBUS_H__

/** Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK_SIZE 32

/**
 * smbus write word
 *   write 2 byte data + 1 byte pec
 */
struct smbus_wr_word {
	uint8_t slave_addr;/**< i2c_addr << 1 */
	uint8_t smbus_cmd; /**< smbus cmd */
	uint8_t data[3];   /**< smbus data */
} __packed;

/**
 * smbus write block data
 * smbus write 1 byte size + 32 byte data + 1 byte pec
 */
struct smbus_wr_block {
	uint8_t slave_addr;/**< i2c_addr << 1 */
	uint8_t smbus_cmd; /**< smbus cmd */
	uint8_t size;      /**< write size */
	uint8_t data[SMBUS_MAX_BLOCK_SIZE+1];
} __packed;

/**
 * smbus read word
 * smbus read 2 byte + 1 pec
 */
struct smbus_rd_word {
	uint8_t slave_addr;   /**< i2c_addr << 1 */
	uint8_t smbus_cmd;    /**< smbus cmd */
	uint8_t slave_addr_rd;/**< (i2c_addr << 1) | 0x1 */
	uint8_t data[3];      /**< smbus data */
} __packed;

/**
 * smbus read block data
 * smbus read 1 byte size + 32 byte data + 1 byte pec
 */
struct smbus_rd_block {
	uint8_t slave_addr;   /**< i2c_addr << 1 */
	uint8_t smbus_cmd;    /**< smbus cmd */
	uint8_t slave_addr_rd;/**< (i2c_addr << 1) | 0x1 */
	uint8_t size;         /**< read block size */
	uint8_t data[SMBUS_MAX_BLOCK_SIZE+1]; /**< smbus data */
} __packed;

/**
 * smbus_write_word
 * smbus write 2 bytes
 * @param i2c_port uint8_t, i2c port address
 * @param slave_addr uint8_t, i2c slave address:= (i2c_addr << 1)
 * @param smbus_cmd uint8_t, smbus command
 * @param d16 uint16_t, 2-bytes data
 * @return error_code
 *       EC_SUCCESS if success; none-zero if fail
 *       EC_ERROR_BUSY if interface is bussy
 *       none zero error code if fail
 */
int smbus_write_word(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint16_t d16);

/**
 * smbus_write_block
 * smbus write upto 32 bytes
 *   case 1:  n-1 byte data, 1 byte PEC
 *     [S][i2c Address][Wr=0][A][cmd][A] ...[Di][Ai]... [PEC][A][P]
 *
 *   case 2:  1 byte data-size, n -2 byte data, 1 byte PEC
 *     [S][i2c Address][Wr=0][A][cmd][A][size][A] ...[Di][Ai]... [PEC][A][P]
 *
 * @param i2c_port uint8_t, i2c port address
 * @param slave_addr uint8_t, i2c slave address:= (i2c_addr << 1)
 * @param smbus_cmd uint8_t, smbus command
 * @param data uint8_t *, n-bytes data
 * @param len uint8_t,  data length
 * @return error_code
 *       EC_SUCCESS if success; none-zero if fail
 *       EC_ERROR_BUSY if interface is bussy
 *       none zero error code if fail
 */
int smbus_write_block(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint8_t *data, uint8_t len);

/**
 * smbus_read_word
 * smbus read 2 bytes
 * @param i2c_port uint8_t, i2c port address
 * @param slave_addr uint8_t, i2c slave address:= (i2c_addr << 1)
 * @param smbus_cmd uint8_t, smbus command
 * @param p16 uint16_t *, a pointer to 2-bytes data
 * @return error_code
 *       EC_SUCCESS if success; none-zero if fail
 *       EC_ERROR_BUSY if interface is bussy
 *       none zero error code if fail
 */
int smbus_read_word(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint16_t *p16);

/**
 * smbus_read_block
 * smbus read upto 32 bytes
 * case 1:  n-1 byte data, 1 byte PEC
 *    [S][i2c addr][Wr=0][A][cmd][A]
 *    [S][i2c addr][Rd=1][A]...[Di][Ai]...[PEC][A][P]
 *
 * case 2:  1 byte data-size, n - 2 byte data, 1 byte PEC
 *    [S][i2c addr][Wr=0][A][cmd][A]
 *    [S][i2c addr][Rd=1][A][size][A]...[Di][Ai]...[PEC][A][P]
 *
 * @param i2c_port uint8_t, i2c port address
 * @param slave_addr uint8_t, i2c slave address:= (i2c_addr << 1)
 * @param smbus_cmd uint8_t, smbus command
 * @param data uint8_t *, n-bytes data
 * @param plen uint8_t *, a pointer data length
 * @return error_code
 *       EC_SUCCESS if success; none-zero if fail
 *       EC_ERROR_BUSY if interface is bussy
 *       none zero error code if fail
 */
int smbus_read_block(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint8_t *data, uint8_t *plen);

/**
 * smbus_read_string
 * smbus read ascii string (upto 32-byte data + 1-byte NULL)
 * Read bytestream from <slaveaddr>:<smbus_cmd> with format:
 *     [length_N] [byte_0] [byte_1] ... [byte_N-1][byte_N='\0']
 *
 * <len>  : the max length of receving buffer. to read N bytes
 *          ascii, len should be at least N+1 to include the
 *          terminating 0 (NULL).
 *
 * @param i2c_port uint8_t, i2c port address
 * @param slave_addr uint8_t, i2c slave address:= (i2c_addr << 1)
 * @param smbus_cmd uint8_t, smbus command
 * @param data uint8_t *, n-bytes data
 * @param len uint8_t,  data length
 * @return error_code
 *       EC_SUCCESS if success; none-zero if fail
 *       EC_ERROR_BUSY if interface is bussy
 *       none zero error code if fail
 */
int smbus_read_string(int i2c_port, uint8_t slave_addr, uint8_t smbus_cmd,
			uint8_t *data, uint8_t len);

#endif /* __EC_SMBUS_H__ */
