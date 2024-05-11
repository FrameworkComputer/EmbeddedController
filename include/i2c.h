/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C interface for Chrome EC */

#ifndef __CROS_EC_I2C_H
#define __CROS_EC_I2C_H

#include "common.h"
#include "gpio_signal.h"
#include "host_command.h"
#include "stddef.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I2C Peripheral Address encoding
 *
 * EC will favor 7bit I2C/SPI address encoding.  The variable/define
 * naming should follow the pattern, if it is just the 7 bit address
 * then end the variable as "addr".  This can be addr, i2c_addr,
 * etc.  If the 7 bit address contains flags for BIG
 * ENDIAN or overloading the address to be a SPI address, then it
 * will be customary to end the variable as "addr_flags".  This can
 * be addr_flags, i2c_addr_flags, etc.
 *
 * Some of the drivers use an 8bit left shifted 7bit address.  Since
 * this is driver specific, it will be up to the driver to make this
 * clear.  I suggest, since this is a very small amount of usage, that
 * ending the variable as "addr_8bit" would make this clear.
 *
 * NOTE: Peripheral addresses are always 16 bit values.  The least significant
 * 10 bits are available as an address.  More significant bits are
 * used here and in motion_sense to give specific meaning to the
 * address that is pertinent to its use.
 */
#define I2C_ADDR_MASK 0x03FF
#define I2C_FLAG_PEC BIT(13)
#define I2C_FLAG_BIG_ENDIAN BIT(14)
/* BIT(15) SPI_FLAG - used in motion_sense to overload address */
#define I2C_FLAG_ADDR_IS_SPI BIT(15)

#define I2C_STRIP_FLAGS(addr_flags) ((addr_flags) & I2C_ADDR_MASK)
#define I2C_USE_PEC(addr_flags) ((addr_flags) & I2C_FLAG_PEC)
#define I2C_IS_BIG_ENDIAN(addr_flags) ((addr_flags) & I2C_FLAG_BIG_ENDIAN)

/*
 * All 7-bit addresses in the following formats
 *   0000 XXX
 *   1111 XXX
 * are reserved for various purposes. Valid 7-bit client adderesses start at
 * 0x08 and end at 0x77 inclusive.
 */
#define I2C_FIRST_VALID_ADDR 0x08
#define I2C_LAST_VALID_ADDR 0x77

/*
 * Max data size for a version 3 request/response packet. This is
 * big enough for EC_CMD_GET_VERSION plus header info.
 */
#define I2C_MAX_HOST_PACKET_SIZE 128

/* The size of the header for a version 3 request packet sent over I2C. */
#define I2C_REQUEST_HEADER_SIZE 1

/* The size of the header for a version 3 response packet sent over I2C. */
#define I2C_RESPONSE_HEADER_SIZE 2

/* This port allows changing speed at runtime */
#define I2C_PORT_FLAG_DYNAMIC_SPEED BIT(0)

#ifndef CONFIG_I2C_BITBANG
#define I2C_BITBANG_PORT_COUNT 0
#endif

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

/*
 * I2C mask update actions.
 *      MASK_SET will OR the mask into the old value
 *      MASK_CLR will AND the ~mask from the old value
 */
enum mask_update_action { MASK_CLR, MASK_SET };

struct i2c_info_t {
	uint16_t port; /* Physical port for device */
	uint16_t addr_flags;
};

struct i2c_port_t; /* forward declaration */

struct i2c_drv {
	int (*xfer)(const struct i2c_port_t *i2c_port,
		    const uint16_t addr_flags, const uint8_t *out, int out_size,
		    uint8_t *in, int in_size, int flags);
};

/* Data structure to define I2C port configuration. */
struct i2c_port_t {
	int port; /* Port */
#ifndef CONFIG_ZEPHYR
	const char *name; /* Port name */
	int kbps; /* Speed in kbps */
	enum gpio_signal scl; /* Port SCL GPIO line */
	enum gpio_signal sda; /* Port SDA GPIO line */
#endif /* CONFIG_ZEPHYR */
	/* When bus is protected, returns true if passthru allowed for address.
	 * If the function is not defined, the default value is true. */
	int (*passthru_allowed)(const struct i2c_port_t *port,
				uint16_t addr_flags);
	const struct i2c_drv *drv;
	uint16_t flags; /* I2C_PORT_FLAG_* flags */
};

extern const struct i2c_port_t i2c_ports[];
extern const unsigned int i2c_ports_used;

#ifdef CONFIG_CMD_I2C_STRESS_TEST
struct i2c_test_reg_info {
	int read_reg; /* Read register (WHO_AM_I, DEV_ID, MAN_ID) */
	int read_val; /* Expected val (WHO_AM_I, DEV_ID, MAN_ID) */
	int write_reg; /* Read/Write reg which doesn't impact the system */
};

struct i2c_test_results {
	int read_success; /* Successful read count */
	int read_fail; /* Read fail count */
	int write_success; /* Successful write count */
	int write_fail; /* Write fail count */
};

/* Data structure to define I2C test configuration. */
struct i2c_stress_test_dev {
	struct i2c_test_reg_info reg_info;
	struct i2c_test_results test_results;
	int (*i2c_read)(const int port, const uint16_t addr_flags,
			const int reg, int *data);
	int (*i2c_write)(const int port, const uint16_t addr_flags,
			 const int reg, int data);
	int (*i2c_read_dev)(const int reg, int *data);
	int (*i2c_write_dev)(const int reg, int data);
};

struct i2c_stress_test {
	int port;
	uint16_t addr_flags;
	struct i2c_stress_test_dev *i2c_test;
};

extern struct i2c_stress_test i2c_stress_tests[];
extern const int i2c_test_dev_used;
#endif

/*
 * Data structure to define I2C Parameters for a command
 */
struct i2c_cmd_desc_t {
	uint8_t port; /* I2C port */
	uint16_t addr_flags; /* Peripheral address and flags */
	uint8_t cmd; /* command, only valid on write operations */
};

/* Flags for i2c_xfer_unlocked() */
#define I2C_XFER_START BIT(0) /* Start smbus session from idle state */
#define I2C_XFER_STOP BIT(1) /* Terminate smbus session with stop bit */
#define I2C_XFER_SINGLE (I2C_XFER_START | I2C_XFER_STOP) /* One transaction */

/**
 * Transmit one block of raw data, then receive one block of raw data. However,
 * transferred data might be capped at CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE if
 * CONFIG_I2C_XFER_LARGE_TRANSFER is not defined.  The transfer is strictly
 * atomic, by locking the I2C port and performing an I2C_XFER_SINGLE transfer.
 *
 * @param port		Port to access
 * @param addr_flags	Peripheral device address
 * @param out		Data to send
 * @param out_size	Number of bytes to send
 * @param in		Destination buffer for received data
 * @param in_size	Number of bytes to receive
 * @return EC_SUCCESS, or non-zero if error.
 */
int i2c_xfer(const int port, const uint16_t addr_flags, const uint8_t *out,
	     int out_size, uint8_t *in, int in_size);

/**
 * Same as i2c_xfer, but the bus is not implicitly locked.  It must be called
 * between i2c_lock(port, 1) and i2c_lock(port, 0).
 *
 * @param flags		Flags (see I2C_XFER_* above)
 */
int i2c_xfer_unlocked(const int port, const uint16_t addr_flags,
		      const uint8_t *out, int out_size, uint8_t *in,
		      int in_size, int flags);

#define I2C_LINE_SCL_HIGH BIT(0)
#define I2C_LINE_SDA_HIGH BIT(1)
#define I2C_LINE_IDLE (I2C_LINE_SCL_HIGH | I2C_LINE_SDA_HIGH)

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
 * Read a 32-bit register from the peripheral at 7-bit peripheral address
 * <addr_flags>, at the specified 8-bit <offset> in the peripheral's address
 * space.
 */
int i2c_read32(const int port, const uint16_t addr_flags, int offset,
	       int *data);

/**
 * Write a 32-bit register to the peripheral at 7-bit peripheral address
 * <addr_flags>, at the specified 8-bit <offset> in the peripheral's address
 * space.
 */
int i2c_write32(const int port, const uint16_t addr_flags, int offset,
		int data);

/**
 * Read a 16-bit register from the peripheral at 7-bit peripheral address
 * <addr_flags>, at the specified 8-bit <offset> in the peripheral's address
 * space.
 */
int i2c_read16(const int port, const uint16_t addr_flags, int offset,
	       int *data);

/**
 * Write a 16-bit register to the peripheral at 7-bit peripheral address
 * <addr_flags>, at the specified 8-bit <offset> in the peripheral's address
 * space.
 */
int i2c_write16(const int port, const uint16_t addr_flags, int offset,
		int data);

/**
 * Read an 8-bit register from the peripheral at 7-bit peripheral address
 * <addr_flags>, at the specified 8-bit <offset> in the peripheral's address
 * space.
 */
int i2c_read8(const int port, const uint16_t addr_flags, int offset, int *data);

/**
 * Write an 8-bit register to the peripheral at 7-bit peripheral address
 * <addr_flags>, at the specified 8-bit <offset> in the peripheral's address
 * space.
 */
int i2c_write8(const int port, const uint16_t addr_flags, int offset, int data);

/**
 * Read, modify, write an i2c register to the peripheral at 7-bit peripheral
 * address <addr_flags> at the specified 8-bit <offset> in the
 * peripheral's address space.  The <action> will specify whether this is
 * setting the <mask> bit value(s) or clearing them. If the value to be written
 * is the same as the original value of the register, the write will not be
 * performed.
 */
int i2c_update8(const int port, const uint16_t addr_flags, const int offset,
		const uint8_t mask, const enum mask_update_action action);

int i2c_update16(const int port, const uint16_t addr_flags, const int offset,
		 const uint16_t mask, const enum mask_update_action action);

/**
 * Read, modify, write field of an i2c register to the peripheral at 7-bit
 * peripheral address <addr_flags>, at the specified 8-bit <offset> in
 * the peripheral's address space.  The register will be read, the <field_mask>
 * will be cleared and then the <set_value> will be set.  The field mask
 * and the set value do not have to be in the same bit locations.  If the
 * new value is not the same as the original value, the new value will be
 * written back out to the device, otherwise no write will be performed.
 */
int i2c_field_update8(const int port, const uint16_t addr_flags,
		      const int offset, const uint8_t field_mask,
		      const uint8_t set_value);

int i2c_field_update16(const int port, const uint16_t addr_flags,
		       const int offset, const uint16_t field_mask,
		       const uint16_t set_value);

/**
 * Read one or two bytes data from the peripheral at 7-bit peripheral address
 * <addr_flags>, at 16-bit <offset> in the peripheral's address space.
 */
int i2c_read_offset16(const int port, const uint16_t addr_flags,
		      uint16_t offset, int *data, int len);

/**
 * Write one or two bytes data to the peripheral at 7-bit peripheral address
 * <addr_flags>, at 16-bit <offset> in the peripheral's address space.
 */
int i2c_write_offset16(const int port, const uint16_t addr_flags,
		       uint16_t offset, int data, int len);

/**
 * Read <len> bytes block data from the peripheral at 7-bit peripheral address
 * * <addr_flags>, at 16-bit <offset> in the peripheral's address space.
 */
int i2c_read_offset16_block(const int port, const uint16_t addr_flags,
			    uint16_t offset, uint8_t *data, int len);

/**
 * Write <len> bytes block data to the peripheral at 7-bit peripheral address
 * <addr_flags>, at 16-bit <offset> in the peripheral's address space.
 */
int i2c_write_offset16_block(const int port, const uint16_t addr_flags,
			     uint16_t offset, const uint8_t *data, int len);

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
 * Read read using  smbus read block protocol.
 * Read bytestream from <addr_flags>:<offset> with format:
 *     [length_N] [byte_0] [byte_1] ... [byte_N]
 *
 * <len>      : the max length of receiving buffer
 */
int i2c_read_sized_block(const int port, const uint16_t addr_flags, int offset,
			 uint8_t *data, int max_len, int *read_len);

/**
 * Read ascii string using smbus read block protocol.
 * Read bytestream from <addr_flags>:<offset> with format:
 *     [length_N] [byte_0] [byte_1] ... [byte_N-1]
 *
 * <len>      : the max length of receiving buffer. to read N bytes
 *              ascii, len should be at least N+1 to include the
 *              terminating 0.  Similar to strlcpy, the terminating null is
 *              always written into the output buffer.
 */
int i2c_read_string(const int port, const uint16_t addr_flags, int offset,
		    uint8_t *data, int len);

/**
 * Read a data block of <len> 8-bit transfers from the peripheral at 7-bit
 * peripheral address <addr_flags>, at the specified 8-bit <offset> in the
 * peripheral's address space.
 */
int i2c_read_block(const int port, const uint16_t addr_flags, int offset,
		   uint8_t *data, int len);

/**
 * Write a data block of <len> 8-bit transfers to the peripheral at 7-bit
 * peripheral address <addr_flags>, at the specified 8-bit <offset> in the
 * peripheral's address space.
 */
int i2c_write_block(const int port, const uint16_t addr_flags, int offset,
		    const uint8_t *data, int len);

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

/**
 * Callbacks processing received data and response
 *
 * i2c_data_received will be called when a peripheral finishes receiving data
 * and i2c_set_response will be called when a peripheral is expected to send a
 * response.
 *
 * Using these, Chrome OS host command protocol should be separated from
 * i2c peripheral drivers (e.g. i2c-stm32f0.c, i2c-stm32f3.c).
 *
 * @param port: I2C port number
 * @param buf:	Buffer containing received data on call and response on return
 * @param len:	Size of received data
 * @return	Size of response data
 */
void i2c_data_received(int port, uint8_t *buf, int len);
int i2c_set_response(int port, uint8_t *buf, int len);

/*
 * Initialize i2c controller. Automatically called at board boot
 * if CONFIG_I2C_CONTROLLER is defined.
 */
void i2c_init(void);

/**
 * Board-level function to determine whether i2c passthru should be allowed
 * on a given I2C command.
 *
 * @parm cmd_desc I2C command
 *
 * @return true, if passthru should be allowed on the I2C command.
 */
__override_proto int
board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc);

/**
 * Board level function that can indicate if a particular i2c bus is known to be
 * currently powered or not.
 *
 * @param port: I2C port number
 *
 * @return non-zero if powered, 0 if the bus is not powered.
 */
int board_is_i2c_port_powered(int port);

/**
 * Function to allow board to take any action before starting a new i2c
 * transaction on a given port. Board must implement this if it defines
 * CONFIG_I2C_XFER_BOARD_CALLBACK.
 *
 * @param port: I2C port number
 * @param addr_flags: Peripheral device address
 *
 */
void i2c_start_xfer_notify(const int port, const uint16_t addr_flags);

/**
 * Function to allow board to take any action after an i2c transaction on a
 * given port has completed. Board must implement this if it defines
 * CONFIG_I2C_XFER_BOARD_CALLBACK.
 *
 * @param port: I2C port number
 * @param addr_flags: Peripheral device address
 *
 */
void i2c_end_xfer_notify(const int port, const uint16_t addr_flags);

/**
 * Defined in common/i2c_trace.c, used by i2c controller to notify tracing
 * functionality of transactions.
 *
 * @param port: I2C port number
 * @param addr_flags: peripheral device address
 * @param out_data: pointer to data written
 * @param out_size: size of data written
 * @param in_data: pointer to data read
 * @param in_size: size of data read
 * @param ret: return of i2c transaction (EC_SUCCESS or otherwise on failure)
 */
void i2c_trace_notify(int port, uint16_t addr_flags, const uint8_t *out_data,
		      size_t out_size, const uint8_t *in_data, size_t in_size,
		      int ret);

/**
 * Convert an enum i2c_freq constant to numeric frequency in kHz.
 *
 * @param freq		An enum i2c_freq constant
 *
 * @return The frequency in kHz, or 0 if the input is not a valid
 *			speed.
 */
int i2c_freq_to_khz(enum i2c_freq freq);

/**
 * Convert a numeric frequency in kHz to an enum i2c_freq constant.
 *
 * @param speed_khz	The frequency in kHz
 *
 * @return The frequency as an enum i2c_freq constant, or I2C_FREQ_COUNT
 *			if the input is not a valid speed.
 */
enum i2c_freq i2c_khz_to_freq(int speed_khz);

/**
 * Set bus speed. Only support for ports with I2C_PORT_FLAG_DYNAMIC_SPEED
 * flag.
 *
 * @param port:		Port to access
 * @param freq:		Bus speed.
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int i2c_set_freq(int port, enum i2c_freq freq);

/**
 * Chip level function to get bus speed.
 *
 * @param port:		Port to access
 *
 * @return Bus speed
 */
enum i2c_freq i2c_get_freq(int port);

/* Find the matching port in i2c_ports[] table. */
const struct i2c_port_t *get_i2c_port(const int port);

#ifdef CONFIG_ZTEST
int i2c_port_is_locked(int port);
#endif

__test_only void i2c_passthru_protect_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_I2C_H */
