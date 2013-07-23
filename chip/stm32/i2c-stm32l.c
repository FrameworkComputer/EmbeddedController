/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_arbitration.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

extern const struct i2c_port_t i2c_ports[I2C_PORTS_USED];

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

#define I2C1      STM32_I2C1_PORT
#define I2C2      STM32_I2C2_PORT

/* Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK 32

/*
 * Transmit timeout in microseconds
 *
 * In theory we shouldn't have a timeout here (at least when we're in slave
 * mode).  The slave is supposed to wait forever for the master to read bytes.
 * ...but we're going to keep the timeout to make sure we're robust.  It may in
 * fact be needed if the host resets itself mid-read.
 */
#define I2C_TX_TIMEOUT_MASTER	(10 * MSEC)

#ifdef CONFIG_I2C_DEBUG
static void dump_i2c_reg(int port, const char *what)
{
	CPRINTF("[%T i2c CR1=%04x CR2=%04x SR1=%04x SR2=%04x %s]\n",
		STM32_I2C_CR1(port),
		STM32_I2C_CR2(port),
		STM32_I2C_SR1(port),
		STM32_I2C_SR2(port),
		what);
}
#else
static inline void dump_i2c_reg(int port, const char *what)
{
}
#endif

/**
 * Wait for SR1 register to contain the specified mask.
 *
 * Returns EC_SUCCESS, EC_ERROR_TIMEOUT if timed out waiting, or
 * EC_ERROR_UNKNOWN if an error bit appeared in the status register.
 */
static int wait_sr1(int port, int mask)
{
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_MASTER;

	while (get_time().val < timeout) {
		int sr1 = STM32_I2C_SR1(port);

		/* Check for desired mask */
		if ((sr1 & mask) == mask)
			return EC_SUCCESS;

		/* Check for errors */
		if (sr1 & (STM32_I2C_SR1_ARLO | STM32_I2C_SR1_BERR |
			   STM32_I2C_SR1_AF)) {
			dump_i2c_reg(port, "wait_sr1 failed");
			return EC_ERROR_UNKNOWN;
		}

		/* I2C is slow, so let other things run while we wait */
		usleep(100);
	}

	/* TODO: on error or timeout, reset port */

	return EC_ERROR_TIMEOUT;
}

/**
 * Send a start condition and slave address on the specified port.
 *
 * @param port		I2C port
 * @param slave_addr	Slave address, with LSB set for receive-mode
 *
 * @return Non-zero if error.
 */
static int send_start(int port, int slave_addr)
{
	int rv;

	/* Send start bit */
	STM32_I2C_CR1(port) |= STM32_I2C_CR1_START;
	dump_i2c_reg(port, "sent start");
	rv = wait_sr1(port, STM32_I2C_SR1_SB);
	if (rv)
		return rv;

	/* Write slave address */
	STM32_I2C_DR(port) = slave_addr & 0xff;
	dump_i2c_reg(port, "wrote addr");
	rv = wait_sr1(port, STM32_I2C_SR1_ADDR);
	if (rv)
		return rv;

	/* Read SR2 to clear ADDR bit */
	rv = STM32_I2C_SR2(port);

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interface */

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_bytes,
	     uint8_t *in, int in_bytes, int flags)
{
	int started = (flags & I2C_XFER_START) ? 0 : 1;
	int rv = EC_SUCCESS;
	int i;

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	dump_i2c_reg(port, "xfer start");

	/* Clear status */
	/*
	 * TODO: should check for any leftover error status, and reset the
	 * port if present.
	 *
	 * Also, may need to wait a bit if a previous STOP hasn't finished
	 * sending yet.
	 */
	STM32_I2C_SR1(port) = 0;

	/* Clear start, stop, POS, ACK bits to get us in a known state */
	STM32_I2C_CR1(port) &= ~(STM32_I2C_CR1_START |
				 STM32_I2C_CR1_STOP |
				 STM32_I2C_CR1_POS |
				 STM32_I2C_CR1_ACK);

	/* No out bytes and no in bytes means just check for active */
	if (out_bytes || !in_bytes) {
		if (!started) {
			rv = send_start(port, slave_addr);
			if (rv)
				goto xfer_exit;
		}

		/* Write data, if any */
		for (i = 0; i < out_bytes; i++) {
			/* Write next data byte */
			STM32_I2C_DR(port) = out[i];
			dump_i2c_reg(port, "wrote data");

			rv = wait_sr1(port, STM32_I2C_SR1_BTF);
			if (rv)
				goto xfer_exit;
		}

		/* Need repeated start condition before reading */
		started = 0;

		/* If no input bytes, queue stop condition */
		if (!in_bytes && (flags & I2C_XFER_STOP))
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;
	}

	if (in_bytes) {
		/* Setup ACK/POS before sending start as per user manual */
		if (in_bytes == 2)
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_POS;
		else if (in_bytes != 1)
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_ACK;

		if (!started) {
			rv = send_start(port, slave_addr | 0x01);
			if (rv)
				goto xfer_exit;
		}

		if (in_bytes == 1) {
			/* Set stop immediately after ADDR cleared */
			if (flags & I2C_XFER_STOP)
				STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;

			rv = wait_sr1(port, STM32_I2C_SR1_RXNE);
			if (rv)
				goto xfer_exit;

			in[0] = STM32_I2C_DR(port);
		} else if (in_bytes == 2) {
			/* Wait till the shift register is full */
			rv = wait_sr1(port, STM32_I2C_SR1_BTF);
			if (rv)
				goto xfer_exit;

			if (flags & I2C_XFER_STOP)
				STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;

			in[0] = STM32_I2C_DR(port);
			in[1] = STM32_I2C_DR(port);
		} else {
			/* Read all but last three */
			for (i = 0; i < in_bytes - 3; i++) {
				/* Wait for receive buffer not empty */
				rv = wait_sr1(port, STM32_I2C_SR1_RXNE);
				if (rv)
					goto xfer_exit;

				dump_i2c_reg(port, "read data");
				in[i] = STM32_I2C_DR(port);
				dump_i2c_reg(port, "post read data");
			}

			/* Wait for BTF (data N-2 in DR, N-1 in shift) */
			rv = wait_sr1(port, STM32_I2C_SR1_BTF);
			if (rv)
				goto xfer_exit;

			/* No more acking */
			STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_ACK;
			in[i++] = STM32_I2C_DR(port);

			/* Wait for BTF (data N-1 in DR, N in shift) */
			rv = wait_sr1(port, STM32_I2C_SR1_BTF);
			if (rv)
				goto xfer_exit;

			/* If this is the last byte, queue stop condition */
			if (flags & I2C_XFER_STOP)
				STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;

			/* Read the last two bytes */
			in[i++] = STM32_I2C_DR(port);
			in[i++] = STM32_I2C_DR(port);
		}
	}

 xfer_exit:
	/* On error, queue a stop condition */
	if (rv) {
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;
		dump_i2c_reg(port, "stop after error");
	}

	return rv;
}

int i2c_get_line_levels(int port)
{
	enum gpio_signal sda, scl;

	ASSERT(port == I2C1 || port == I2C2);

	if (port == I2C1) {
		sda = GPIO_I2C1_SDA;
		scl = GPIO_I2C1_SCL;
	} else {
		sda = GPIO_I2C2_SDA;
		scl = GPIO_I2C2_SCL;
	}

	return (gpio_get_level(sda) ? I2C_LINE_SDA_HIGH : 0) |
		(gpio_get_level(scl) ? I2C_LINE_SCL_HIGH : 0);
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
	int len)
{
	int rv;
	uint8_t reg, block_length;

	/*
	 * TODO: when i2c_xfer() supports start/stop bits, won't need a temp
	 * buffer, and this code can merge with the LM4 implementation and
	 * move to i2c_common.c.
	 */
	uint8_t buffer[SMBUS_MAX_BLOCK + 1];

	if ((len <= 0) || (len > SMBUS_MAX_BLOCK))
		return EC_ERROR_INVAL;

	i2c_lock(port, 1);

	reg = offset;
	rv = i2c_xfer(port, slave_addr, &reg, 1, buffer, SMBUS_MAX_BLOCK + 1,
		      I2C_XFER_SINGLE);
	if (rv == EC_SUCCESS) {
		/* Block length is the first byte of the returned buffer */
		block_length = MIN(buffer[0], len - 1);
		buffer[block_length + 1] = 0;

		memcpy(data, buffer+1, block_length + 1);
	}

	i2c_lock(port, 0);
	return rv;
}

/*****************************************************************************/
/* Hooks */

/* Handle CPU clock changing frequency */
static void i2c_freq_change(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int freq = clock_get_freq();
	int i;

	for (i = 0; i < I2C_PORTS_USED; i++, p++) {
		int port = p->port;

		/* Force peripheral reset and disable port */
		STM32_I2C_CR1(port) = STM32_I2C_CR1_SWRST;
		STM32_I2C_CR1(port) = 0;

		/* Set clock frequency */
		STM32_I2C_CCR(port) = freq / (2 * MSEC * p->kbps);
		STM32_I2C_CR2(port) = freq / SECOND;
		STM32_I2C_TRISE(port) = freq / SECOND + 1;

		/* Enable port */
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_PE;
	}
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_change, HOOK_PRIO_DEFAULT);

static void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < I2C_PORTS_USED; i++, p++) {
		int port = p->port;

		/* Enable clocks to I2C modules if necessary */
		if (!(STM32_RCC_APB1ENR & (1 << (21 + port)))) {
			/* TODO: unwedge bus if necessary */
			STM32_RCC_APB1ENR |= 1 << (21 + port);
		}
	}

	/* Set up initial bus frequencies */
	i2c_freq_change();

	/* TODO: enable interrupts using I2C_CR2 bits 8,9 */
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

static int command_i2c(int argc, char **argv)
{
	int rw = 0;
	int slave_addr, offset;
	int value = 0;
	char *e;
	int rv = 0;

	if (argc < 4) {
		ccputs("Usage: i2c r/r16/w/w16 slave_addr offset [value]\n");
		return EC_ERROR_UNKNOWN;
	}

	if (strcasecmp(argv[1], "r") == 0) {
		rw = 0;
	} else if (strcasecmp(argv[1], "r16") == 0) {
		rw = 1;
	} else if (strcasecmp(argv[1], "w") == 0) {
		rw = 2;
	} else if (strcasecmp(argv[1], "w16") == 0) {
		rw = 3;
	} else {
		ccputs("Invalid rw mode : r / w / r16 / w16\n");
		return EC_ERROR_INVAL;
	}

	slave_addr = strtoi(argv[2], &e, 0);
	if (*e) {
		ccputs("Invalid slave_addr\n");
		return EC_ERROR_INVAL;
	}

	offset = strtoi(argv[3], &e, 0);
	if (*e) {
		ccputs("Invalid addr\n");
		return EC_ERROR_INVAL;
	}

	if (rw > 1) {
		if (argc < 5) {
			ccputs("No write value\n");
			return EC_ERROR_INVAL;
		}
		value = strtoi(argv[4], &e, 0);
		if (*e) {
			ccputs("Invalid write value\n");
			return EC_ERROR_INVAL;
		}
	}


	switch (rw) {
	case 0:
		rv = i2c_read8(I2C_PORT_HOST, slave_addr, offset, &value);
		break;
	case 1:
		rv = i2c_read16(I2C_PORT_HOST, slave_addr, offset, &value);
		break;
	case 2:
		rv = i2c_write8(I2C_PORT_HOST, slave_addr, offset, value);
		break;
	case 3:
		rv = i2c_write16(I2C_PORT_HOST, slave_addr, offset, value);
		break;
	}


	if (rv) {
		ccprintf("i2c command failed\n", rv);
		return rv;
	}

	if (rw == 0)
		ccprintf("0x%02x [%d]\n", value);
	else if (rw == 1)
		ccprintf("0x%04x [%d]\n", value);

	ccputs("ok\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cxfer, command_i2c,
			"r/r16/w/w16 slave_addr offset [value]",
			"Read write I2C",
			NULL);

static int command_i2cdump(int argc, char **argv)
{
	dump_i2c_reg(I2C_PORT_HOST, "dump");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cdump, command_i2cdump,
			NULL,
			"Dump I2C regs",
			NULL);
