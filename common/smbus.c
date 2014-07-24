/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * smbus cross-platform code for Chrome EC
 *  ref: http://smbus.org/specs/smbus20.pdf
 */

#include "common.h"
#include "console.h"
#include "util.h"
#include "i2c.h"
#include "smbus.h"
#include "crc8.h"
#include "shared_mem.h"

/**
 * @brief smbus write common interface
 *  [S][slave_addr][A][smbus_cmd][A]...[P]
 */
struct smbus_wr_if {
	uint8_t  slave_addr;/**< i2c_addr << 1 */
	uint8_t  smbus_cmd; /**< smbus cmd */
	uint8_t  data[0];   /**< smbus data */
} __packed;

/**
 * @brief smbus read common interface
 *  [S][slave_addr][A][smbus_cmd][A][slave_addr_rd][A]...[P]
 */
struct smbus_rd_if {
	uint8_t slave_addr;   /**< (i2c_addr << 1)*/
	uint8_t smbus_cmd;    /**< smbus cmd */
	uint8_t slave_addr_rd;/**< (i2c_addr << 1) | 0x1 */
	uint8_t data[0];      /**< smbus data */
} __packed;


#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/*
 * smbus interface write n bytes
 *   case 1:  n-1 byte data, 1 byte PEC
 *     [S][i2c Address][Wr=0][A][cmd][A] ...[Di][Ai]... [PEC][A][P]
 *
 *   case 2:  1 byte data-size, n -2 byte data, 1 byte PEC
 *     [S][i2c Address][Wr=0][A][cmd][A][size][A] ...[Di][Ai]... [PEC][A][P]
 */
static int smbus_if_write(int i2c_port, struct smbus_wr_if *intf,
			uint8_t size_n, uint8_t data_n, uint8_t pec_n)
{
	int rv;
	uint8_t n;
	data_n = MIN(data_n, SMBUS_MAX_BLOCK_SIZE);
	n = size_n + data_n + pec_n;
	if (pec_n)
		intf->data[n-1] = crc8((const uint8_t *)intf,
				n - 1 + sizeof(struct smbus_wr_if));
	i2c_lock(i2c_port, 1);
	rv = i2c_is_busy(i2c_port);
	if (!rv)
		rv = i2c_xfer(i2c_port, intf->slave_addr,
			&intf->smbus_cmd, n + 1, NULL, 0, I2C_XFER_SINGLE);
	else
		rv = EC_ERROR_BUSY;
	i2c_lock(i2c_port, 0);
	if (rv)
		CPRINTF("smbus wr i2c_xfer error:%d cmd:%02X n:%d\n",
			rv, intf->smbus_cmd, n);
	return rv;
}

/*
 * smbus interface read n bytes
 *   tx 8-bit smbus cmd, and read n bytes
 *
 * case 1:  n-1 byte data, 1 byte PEC
 *    [S][i2c addr][Wr=0][A][cmd][A]
 *    [S][i2c addr][Rd=1][A]...[Di][Ai]...[PEC][A][P]
 *
 * case 2:  1 byte data-size, n - 2 byte data, 1 byte PEC
 *    [S][i2c addr][Wr=0][A][cmd][A]
 *    [S][i2c addr][Rd=1][A][size][A]...[Di][Ai]...[PEC][A][P]
 */
static int smbus_if_read(int i2c_port, struct smbus_rd_if *intf,
		uint8_t size_n, uint8_t *pdata_n, uint8_t pec_n)
{
	int rv;
	uint8_t pec, n, data_n;

	data_n = MIN(*pdata_n, SMBUS_MAX_BLOCK_SIZE);
	n = size_n + data_n + pec_n;

	i2c_lock(i2c_port, 1);

	/* Check if smbus is busy */
	rv = i2c_is_busy(i2c_port);
	if (rv) {
		rv = EC_ERROR_BUSY;
		CPRINTF("smbus_cmd:%02X bus busy error:%d\n",
			intf->smbus_cmd, rv);
		i2c_lock(i2c_port, 0);
		return rv;
	}

	rv = i2c_xfer(i2c_port, intf->slave_addr,
			&(intf->smbus_cmd), 1, intf->data, n, I2C_XFER_SINGLE);

	i2c_lock(i2c_port, 0);

	if (rv)
		return rv;

	if (pec_n == 0)
		return EC_SUCCESS;

	/*
	 * Compute and Check Packet Error Code (crc8)
	 */
	intf->slave_addr_rd = intf->slave_addr | 0x01;
	if (size_n) {
		data_n = MIN(data_n, intf->data[0]);
		data_n = MIN(data_n, SMBUS_MAX_BLOCK_SIZE);
	}

	if (*pdata_n != data_n) {
		CPRINTF("smbus read[%02X] size %02X != %02X\n",
			intf->smbus_cmd, *pdata_n, data_n);
		return EC_ERROR_INVAL;
	}

	n = size_n + data_n + pec_n;
	pec = crc8((const uint8_t *)intf, n - 1 + sizeof(struct smbus_rd_if));
	if (pec != intf->data[n-1]) {
		CPRINTF("smbus read[%02X] PEC %02X != %02X\n",
			intf->smbus_cmd, intf->data[n-1], pec);
		return EC_ERROR_CRC;
	}
	return EC_SUCCESS;
}

int smbus_write_word(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint16_t d16)
{
	int rv;
	struct smbus_wr_word *s;
	rv = shared_mem_acquire(sizeof(struct smbus_wr_word), (char **)&s);
	if (rv) {
		CPRINTF("smbus write wd[%02X] mem error\n", smbus_cmd);
		return rv;
	}
	s->slave_addr = slave_addr,
	s->smbus_cmd = smbus_cmd;
	s->data[0] = d16 & 0xFF;
	s->data[1] = (d16 >> 8) & 0xFF;
	rv = smbus_if_write(i2c_port, (struct smbus_wr_if *)s, 0, 2, 1);
	shared_mem_release(s);
	return rv;
}

int smbus_write_block(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint8_t *data, uint8_t len)
{
	int rv;
	struct smbus_wr_block *s;
	rv = shared_mem_acquire(sizeof(struct smbus_wr_block), (char **)&s);
	if (rv) {
		CPRINTF("smbus write block[%02X] mem error\n",
			smbus_cmd);
		return rv;
	}
	s->slave_addr = slave_addr,
	s->smbus_cmd = smbus_cmd;
	s->size = MIN(len, SMBUS_MAX_BLOCK_SIZE);
	memmove(s->data, data, s->size);
	rv = smbus_if_write(i2c_port, (struct smbus_wr_if *)s, 1, s->size, 1);
	shared_mem_release(s);
	return rv;
}

int smbus_read_word(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint16_t *p16)
{
	int rv;
	uint8_t data_n = 2;
	struct smbus_rd_word s;
	s.slave_addr = slave_addr;
	s.smbus_cmd = smbus_cmd;
	rv = smbus_if_read(i2c_port, (struct smbus_rd_if *)&s, 0, &data_n, 1);
	if (rv == EC_SUCCESS)
		*p16 = (s.data[1] << 8) | s.data[0];
	else
		*p16 = 0;
	return rv;
}

int smbus_read_block(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint8_t *data, uint8_t *plen)
{
	int rv;
	struct smbus_rd_block *s;
	uint8_t len = *plen;
	rv = shared_mem_acquire(sizeof(struct smbus_rd_block), (char **)&s);

	if (rv) {
		CPRINTF("smbus read block[%02X] mem error\n",
			smbus_cmd);
		return rv;
	}
	s->slave_addr = slave_addr,
	s->smbus_cmd = smbus_cmd;
	s->size = MIN(len, SMBUS_MAX_BLOCK_SIZE);

	rv = smbus_if_read(i2c_port, (struct smbus_rd_if *)s, 1, &s->size, 1);
	s->size = MIN(s->size, SMBUS_MAX_BLOCK_SIZE);
	s->size = MIN(s->size, len);
	*plen = s->size;
	if (rv == EC_SUCCESS)
		memmove(data, s->data, s->size);
	else
		memset(data, 0x0, s->size);

	shared_mem_release(s);
	return rv;
}

int smbus_read_string(int i2c_port, uint8_t slave_addr, uint8_t smbus_cmd,
			uint8_t *data, uint8_t len)
{
	int rv;
	len -= 1;
	rv = smbus_read_block(i2c_port, slave_addr,
			smbus_cmd, data, &len);
	data[len] = '\0';
	return rv;
}
