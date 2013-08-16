/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Define CONFIG_CMD_I2CWEDGE and I2C_PORT_HOST to enable the 'i2cwedge'
 * console command to allow us to bang the bus into a wedged state. For
 * example, include the following lines in board/pit/board.h to enable it on
 * pit:
 *
 * #define CONFIG_CMD_I2CWEDGE
 * #define I2C_PORT_HOST I2C_PORT_MASTER
 *
 */

#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "util.h"

/*
 * The implementation is based on Wikipedia.
 */

int i2c_bang_started;

static void i2c_bang_delay(void)
{
	udelay(5);
}

static void i2c_bang_start_cond(void)
{
	/* Restart if needed */
	if (i2c_bang_started) {
		/* set SDA to 1 */
		i2c_raw_set_sda(I2C_PORT_HOST, 1);
		i2c_bang_delay();

		/* Clock stretching */
		i2c_raw_set_scl(I2C_PORT_HOST, 1);
		while (i2c_raw_get_scl(I2C_PORT_HOST) == 0)
			; /* TODO(crosbug.com/p/26487): TIMEOUT */

		/* Repeated start setup time, minimum 4.7us */
		i2c_bang_delay();
	}

	i2c_raw_set_sda(I2C_PORT_HOST, 1);
	if (i2c_raw_get_sda(I2C_PORT_HOST) == 0)
		; /* TODO(crosbug.com/p/26487): arbitration_lost */

	/* SCL is high, set SDA from 1 to 0. */
	i2c_raw_set_sda(I2C_PORT_HOST, 0);
	i2c_bang_delay();
	i2c_raw_set_scl(I2C_PORT_HOST, 0);
	i2c_bang_started = 1;

	ccputs("BITBANG: send start\n");
}

static void i2c_bang_stop_cond(void)
{
	/* set SDA to 0 */
	i2c_raw_set_sda(I2C_PORT_HOST, 0);
	i2c_bang_delay();

	/* Clock stretching */
	i2c_raw_set_scl(I2C_PORT_HOST, 1);
	while (i2c_raw_get_scl(I2C_PORT_HOST) == 0)
		; /* TODO(crosbug.com/p/26487): TIMEOUT */

	/* Stop bit setup time, minimum 4us */
	i2c_bang_delay();

	/* SCL is high, set SDA from 0 to 1 */
	i2c_raw_set_sda(I2C_PORT_HOST, 1);
	if (i2c_raw_get_sda(I2C_PORT_HOST) == 0)
		; /* TODO(crosbug.com/p/26487): arbitration_lost */

	i2c_bang_delay();

	i2c_bang_started = 0;
	ccputs("BITBANG: send stop\n");
}

static void i2c_bang_out_bit(int bit)
{
	if (bit)
		i2c_raw_set_sda(I2C_PORT_HOST, 1);
	else
		i2c_raw_set_sda(I2C_PORT_HOST, 0);

	i2c_bang_delay();

	/* Clock stretching */
	i2c_raw_set_scl(I2C_PORT_HOST, 1);
	while (i2c_raw_get_scl(I2C_PORT_HOST) == 0)
		; /* TODO(crosbug.com/p/26487): TIMEOUT */

	/*
	 * SCL is high, now data is valid
	 * If SDA is high, check that nobody else is driving SDA
	 */
	i2c_raw_set_sda(I2C_PORT_HOST, 1);
	if (bit && i2c_raw_get_sda(I2C_PORT_HOST) == 0)
		; /* TODO(crosbug.com/p/26487): arbitration_lost */

	i2c_bang_delay();
	i2c_raw_set_scl(I2C_PORT_HOST, 0);
}

static int i2c_bang_in_bit(void)
{
	int bit;

	/* Let the slave drive data */
	i2c_raw_set_sda(I2C_PORT_HOST, 1);
	i2c_bang_delay();

	/* Clock stretching */
	i2c_raw_set_scl(I2C_PORT_HOST, 1);
	while (i2c_raw_get_scl(I2C_PORT_HOST) == 0)
		; /* TODO(crosbug.com/p/26487): TIMEOUT */

	/* SCL is high, now data is valid */
	bit = i2c_raw_get_sda(I2C_PORT_HOST);
	i2c_bang_delay();
	i2c_raw_set_scl(I2C_PORT_HOST, 0);

	return bit;
}

/* Write a byte to I2C bus. Return 0 if ack by the slave. */
static int i2c_bang_out_byte(int send_start, int send_stop, unsigned char byte)
{
	unsigned bit;
	int nack;
	int tmp = byte;

	if (send_start)
		i2c_bang_start_cond();

	for (bit = 0; bit < 8; bit++) {
		i2c_bang_out_bit((byte & 0x80) != 0);
		byte <<= 1;
	}

	nack = i2c_bang_in_bit();

	ccprintf("  write byte: %d     ack/nack=%d\n", tmp, nack);

	if (send_stop)
		i2c_bang_stop_cond();

	return nack;
}

static unsigned char i2c_bang_in_byte(int ack, int send_stop)
{
	unsigned char byte = 0;
	int i;
	for (i = 0; i < 8; ++i)
		byte = (byte << 1) | i2c_bang_in_bit();
	i2c_bang_out_bit(ack != 0);
	if (send_stop)
		i2c_bang_stop_cond();
	return byte;
}

static void i2c_bang_init(void)
{
	i2c_bang_started = 0;

	i2c_raw_mode(I2C_PORT_HOST, 1);
}

static void i2c_bang_xfer(int slave_addr, int reg)
{
	int byte;

	i2c_bang_init();

	/* State a write command to 'slave_addr' */
	i2c_bang_out_byte(1 /*start*/, 0 /*stop*/, slave_addr);
	/* Write 'reg' */
	i2c_bang_out_byte(0 /*start*/, 0 /*stop*/, reg);

	/* Start a read command */
	i2c_bang_out_byte(1 /*start*/, 0 /*stop*/, slave_addr | 1);

	/* Read two bytes */
	byte = i2c_bang_in_byte(0, 0); /* ack and no stop */
	ccprintf("  read byte: %d\n", byte);
	byte = i2c_bang_in_byte(1, 1); /* nack and stop */
	ccprintf("  read byte: %d\n", byte);
}

static void i2c_bang_wedge_write(int slave_addr, int byte, int bit_count,
	int reboot)
{
	int i;

	i2c_bang_init();

	/* State a write command to 'slave_addr' */
	i2c_bang_out_byte(1 /*start*/, 0 /*stop*/, slave_addr);
	/* Send a few bits and stop */
	for (i = 0; i < bit_count; ++i) {
		i2c_bang_out_bit((byte & 0x80) != 0);
		byte <<= 1;
	}
	ccprintf("  wedged write after %d bits\n", bit_count);

	if (reboot)
		system_reset(0);
}

static void i2c_bang_wedge_read(int slave_addr, int reg, int bit_count,
	int reboot)
{
	int i;

	i2c_bang_init();

	/* State a write command to 'slave_addr' */
	i2c_bang_out_byte(1 /*start*/, 0 /*stop*/, slave_addr);
	/* Write 'reg' */
	i2c_bang_out_byte(0 /*start*/, 0 /*stop*/, reg);

	/* Start a read command */
	i2c_bang_out_byte(1 /*start*/, 0 /*stop*/, slave_addr | 1);

	/* Read bit_count bits and stop */
	for (i = 0; i < bit_count; ++i)
		i2c_bang_in_bit();

	ccprintf("  wedged read after %d bits\n", bit_count);

	if (reboot)
		system_reset(0);
}

#define WEDGE_WRITE	1
#define WEDGE_READ	2
#define WEDGE_REBOOT	4

static int command_i2c_wedge(int argc, char **argv)
{
	int slave_addr, reg, wedge_flag = 0, wedge_bit_count = -1;
	char *e;
	enum gpio_signal tmp;

	/* Verify that the I2C_PORT_HOST has SDA and SCL pins defined. */
	if (get_sda_from_i2c_port(I2C_PORT_HOST, &tmp) != EC_SUCCESS ||
		get_scl_from_i2c_port(I2C_PORT_HOST, &tmp) != EC_SUCCESS) {
		ccprintf("Cannot wedge bus because no SCL and SDA pins are"
			"defined for this port. Check i2c_ports[].\n");
		return EC_SUCCESS;
	}

	if (argc < 3) {
		ccputs("Usage: i2cwedge slave_addr out_byte "
			"[wedge_flag [wedge_bit_count]]\n");
		ccputs("  wedge_flag - (1: wedge out; 2: wedge in;"
			" 5: wedge out+reboot; 6: wedge in+reboot)]\n");
		ccputs("  wedge_bit_count - 0 to 8\n");
		return EC_ERROR_UNKNOWN;
	}

	slave_addr = strtoi(argv[1], &e, 0);
	if (*e) {
		ccprintf("Invalid slave_addr %s\n", argv[1]);
		return EC_ERROR_INVAL;
	}
	reg = strtoi(argv[2], &e, 0);
	if (*e) {
		ccprintf("Invalid out_byte %s\n", argv[2]);
		return EC_ERROR_INVAL;
	}
	if (argc > 3) {
		wedge_flag = strtoi(argv[3], &e, 0);
		if (*e) {
			ccprintf("Invalid wedge_flag %s\n", argv[3]);
			return EC_ERROR_INVAL;
		}
	}
	if (argc > 4) {
		wedge_bit_count = strtoi(argv[4], &e, 0);
		if (*e || wedge_bit_count < 0 || wedge_bit_count > 8) {
			ccprintf("Invalid wedge_bit_count %s.\n", argv[4]);
			return EC_ERROR_INVAL;
		}
	}

	i2c_lock(I2C_PORT_HOST, 1);

	if (wedge_flag & WEDGE_WRITE) {
		if (wedge_bit_count < 0)
			wedge_bit_count = 8;
		i2c_bang_wedge_write(slave_addr, reg, wedge_bit_count,
			(wedge_flag & WEDGE_REBOOT));
	} else if (wedge_flag & WEDGE_READ) {
		if (wedge_bit_count < 0)
			wedge_bit_count = 2;
		i2c_bang_wedge_read(slave_addr, reg, wedge_bit_count,
			(wedge_flag & WEDGE_REBOOT));
	} else {
		i2c_bang_xfer(slave_addr, reg);
	}

	/* Put it back into normal mode */
	i2c_raw_mode(I2C_PORT_HOST, 0);

	i2c_lock(I2C_PORT_HOST, 0);

	if (wedge_flag & (WEDGE_WRITE | WEDGE_READ))
		ccprintf("I2C bus %d is now wedged. Enjoy.\n", I2C_PORT_HOST);
	else
		ccprintf("Bit bang xfer complete.\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cwedge, command_i2c_wedge,
			"i2cwedge slave_addr out_byte "
				"[wedge_flag [wedge_bit_count]]",
			"Wedge host I2C bus",
			NULL);

static int command_i2c_unwedge(int argc, char **argv)
{
	i2c_unwedge(I2C_PORT_HOST);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cunwedge, command_i2c_unwedge,
	"",
	"Unwedge host I2C bus",
	NULL);

