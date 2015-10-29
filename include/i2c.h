/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C interface for Chrome EC */

#ifndef __CROS_EC_I2C_H
#define __CROS_EC_I2C_H

#include "common.h"

/* Flags for slave address field, in addition to the 8-bit address */
#define I2C_FLAG_BIG_ENDIAN 0x100  /* 16 byte values are MSB-first */

/*
 * Supported I2C CLK frequencies.
 * TODO(crbug.com/549286): Use this enum in i2c_port_t.
 */
enum i2c_freq {
	I2C_FREQ_1000KHZ = 0,
	I2C_FREQ_400KHZ = 1,
	I2C_FREQ_100KHZ = 2,
	I2C_FREQ_COUNT,
};

/* Data structure to define I2C port configuration. */
struct i2c_port_t {
	const char *name;     /* Port name */
	int port;             /* Port */
	int kbps;             /* Speed in kbps */
	enum gpio_signal scl; /* Port SCL GPIO line */
	enum gpio_signal sda; /* Port SDA GPIO line */
};

extern const struct i2c_port_t i2c_ports[];
extern const unsigned int i2c_ports_used;

/* Flags for i2c_xfer() */
#define I2C_XFER_START (1 << 0)  /* Start smbus session from idle state */
#define I2C_XFER_STOP (1 << 1)  /* Terminate smbus session with stop bit */
#define I2C_XFER_SINGLE (I2C_XFER_START | I2C_XFER_STOP)  /* One transaction */

/**
 * Transmit one block of raw data, then receive one block of raw data.
 *
 * This is a wrapper function for chip_i2c_xfer(), a low-level chip-dependent
 * function. It must be called between i2c_lock(port, 1) and i2c_lock(port, 0).
 *
 * @param port		Port to access
 * @param slave_addr	Slave device address
 * @param out		Data to send
 * @param out_size	Number of bytes to send
 * @param in		Destination buffer for received data
 * @param in_size	Number of bytes to receive
 * @param flags		Flags (see I2C_XFER_* above)
 * @return EC_SUCCESS, or non-zero if error.
 */
int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags);

#define I2C_LINE_SCL_HIGH (1 << 0)
#define I2C_LINE_SDA_HIGH (1 << 1)
#define I2C_LINE_IDLE (I2C_LINE_SCL_HIGH | I2C_LINE_SDA_HIGH)

/**
 * Chip-level function to transmit one block of raw data, then receive one
 * block of raw data.
 *
 * This is a low-level chip-dependent function and should only be called by
 * i2c_xfer().
 *
 * @param port		Port to access
 * @param slave_addr	Slave device address
 * @param out		Data to send
 * @param out_size	Number of bytes to send
 * @param in		Destination buffer for received data
 * @param in_size	Number of bytes to receive
 * @param flags		Flags (see I2C_XFER_* above)
 * @return EC_SUCCESS, or non-zero if error.
 */
int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags);

/**
 * Return raw I/O line levels (I2C_LINE_*) for a port when port is in alternate
 * function mode.
 *
 * @param port		Port to check
 */
int i2c_get_line_levels(int port);

/**
 * Get GPIO pin for I2C SCL from the i2c port number
 *
 * @param port I2C port number
 * @param sda  Pointer to gpio signal to store the SCL gpio at
 * @return EC_SUCCESS if a valid GPIO point is found, EC_ERROR_INVAL if not
 */
int get_scl_from_i2c_port(int port, enum gpio_signal *scl);

/**
 * Get GPIO pin for I2C SDA from the i2c port number
 *
 * @param port I2C port number
 * @param sda  Pointer to gpio signal to store the SDA gpio at
 * @return EC_SUCCESS if a valid GPIO point is found, EC_ERROR_INVAL if not
 */
int get_sda_from_i2c_port(int port, enum gpio_signal *sda);

/**
 * Get the state of the SCL pin when port is not in alternate function mode.
 *
 * @param port		I2C port of interest
 * @return		State of SCL pin
 */
int i2c_raw_get_scl(int port);

/**
 * Get the state of the SDA pin when port is not in alternate function mode.
 *
 * @param port		I2C port of interest
 * @return		State of SDA pin
 */
int i2c_raw_get_sda(int port);

/**
 * Set the state of the SCL pin.
 *
 * @param port		I2C port of interest
 * @param level		State to set SCL pin to
 */
void i2c_raw_set_scl(int port, int level);

/**
 * Set the state of the SDA pin.
 *
 * @param port		I2C port of interest
 * @param level		State to set SDA pin to
 */
void i2c_raw_set_sda(int port, int level);

/**
 * Toggle the I2C pins into or out of raw / big-bang mode.
 *
 * @param port		I2C port of interest
 * @param enable	Flag to enable raw mode or disable it
 * @return		EC_SUCCESS if successful
 */
int i2c_raw_mode(int port, int enable);

/**
 * Lock / unlock an I2C port.
 * @param port		Port to lock
 * @param lock		1 to lock, 0 to unlock
 */
void i2c_lock(int port, int lock);

/* Default maximum time we allow for an I2C transfer */
#define I2C_TIMEOUT_DEFAULT_US (100 * MSEC)

/**
 * Prepare I2C module for sysjump.
 */
void i2c_prepare_sysjump(void);

/**
 * Set the timeout for an I2C transaction.
 *
 * @param port		Port to set timeout for
 * @param timeout	Timeout in usec, or 0 to use default
 */
void i2c_set_timeout(int port, uint32_t timeout);

/**
 * Read a 32-bit register from the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space.
 */
int i2c_read32(int port, int slave_addr, int offset, int *data);

/**
 * Write a 32-bit register to the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space.
 */
int i2c_write32(int port, int slave_addr, int offset, int data);

/**
 * Read a 16-bit register from the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space.
 */
int i2c_read16(int port, int slave_addr, int offset, int *data);

/**
 * Write a 16-bit register to the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space.
 */
int i2c_write16(int port, int slave_addr, int offset, int data);

/**
 * Read an 8-bit register from the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space.
 */
int i2c_read8(int port, int slave_addr, int offset, int *data);

/**
 * Write an 8-bit register to the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space.
 */
int i2c_write8(int port, int slave_addr, int offset, int data);

/**
 * @return non-zero if i2c bus is busy
 */
int i2c_is_busy(int port);

/**
 * Attempt to unwedge an I2C bus.
 *
 * @param port I2C port
 *
 * @return EC_SUCCESS or EC_ERROR_UNKNOWN
 */
int i2c_unwedge(int port);

/**
 * Read ascii string using smbus read block protocol.
 * Read bytestream from <slaveaddr>:<offset> with format:
 *     [length_N] [byte_0] [byte_1] ... [byte_N-1]
 *
 * <len>      : the max length of receving buffer. to read N bytes
 *              ascii, len should be at least N+1 to include the
 *              terminating 0.
 * <len> == 0 : buffer size > 255
 */
int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
			int len);

/**
 * Convert port number to controller number, for multi-port controllers.
 * This function will only be called if CONFIG_I2C_MULTI_PORT_CONTROLLER is
 * defined.
 *
 * @parm port I2C port
 *
 * @return controller number, or -1 on invalid parameter
 */
int i2c_port_to_controller(int port);

#endif  /* __CROS_EC_I2C_H */
