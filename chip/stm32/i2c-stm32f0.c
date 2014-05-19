/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

/* Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK 32

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT_MASTER	(10 * MSEC)

/**
 * Wait for ISR register to contain the specified mask.
 *
 * Returns EC_SUCCESS, EC_ERROR_TIMEOUT if timed out waiting, or
 * EC_ERROR_UNKNOWN if an error bit appeared in the status register.
 */
static int wait_isr(int port, int mask)
{
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_MASTER;

	while (get_time().val < timeout) {
		int isr = STM32_I2C_ISR(port);

		/* Check for desired mask */
		if ((isr & mask) == mask)
			return EC_SUCCESS;

		/* Check for errors */
		if (isr & (STM32_I2C_ISR_ARLO | STM32_I2C_ISR_BERR))
			return EC_ERROR_UNKNOWN;

		/* I2C is slow, so let other things run while we wait */
		usleep(100);
	}

	return EC_ERROR_TIMEOUT;
}

static void i2c_set_freq_port(const struct i2c_port_t *p)
{
	int port = p->port;

	/* Disable port */
	STM32_I2C_CR1(port) = 0;
	STM32_I2C_CR2(port) = 0;
	/* Set clock frequency */
	switch (p->kbps) {
	case 1000:
		STM32_I2C_TIMINGR(port) = 0x50110103;
		break;
	case 400:
		STM32_I2C_TIMINGR(port) = 0x50330309;
		break;
	case 100:
		STM32_I2C_TIMINGR(port) = 0xB0420F13;
		break;
	default: /* unknown speed, defaults to 100kBps */
		CPRINTS("I2C bad speed %d kBps", p->kbps);
		STM32_I2C_TIMINGR(port) = 0xB0420F13;
	}
	/* Enable port */
	STM32_I2C_CR1(port) = STM32_I2C_CR1_PE;
}

/**
 * Initialize on the specified I2C port.
 *
 * @param p		the I2c port
 */
static void i2c_init_port(const struct i2c_port_t *p)
{
	int port = p->port;

	/* Enable clocks to I2C modules if necessary */
	if (!(STM32_RCC_APB1ENR & (1 << (21 + port))))
		STM32_RCC_APB1ENR |= 1 << (21 + port);

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* Set up initial bus frequencies */
	i2c_set_freq_port(p);
}

/*****************************************************************************/
/* Interface */

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_bytes,
	     uint8_t *in, int in_bytes, int flags)
{
	int rv = EC_SUCCESS;
	int i;

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	/* Clear status */
	STM32_I2C_ICR(port) = 0x3F38;
	STM32_I2C_CR2(port) = 0;

	if (out_bytes || !in_bytes) {
		/* Configure the write transfer */
		STM32_I2C_CR2(port) =  ((out_bytes & 0xFF) << 16)
			| slave_addr
			| (in_bytes == 0 ? STM32_I2C_CR2_AUTOEND : 0);
		/* let's go ... */
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_START;

		for (i = 0; i < out_bytes; i++) {
			rv = wait_isr(port, STM32_I2C_ISR_TXIS);
			if (rv)
				goto xfer_exit;
			/* Write next data byte */
			STM32_I2C_TXDR(port) = out[i];
		}
	}
	if (in_bytes) {
		if (out_bytes) { /* wait for completion of the write */
			rv = wait_isr(port, STM32_I2C_ISR_TC);
			if (rv)
				goto xfer_exit;
		}
		/* Configure the read transfer */
		STM32_I2C_CR2(port) = ((in_bytes & 0xFF) << 16)
				    | STM32_I2C_CR2_RD_WRN | slave_addr
				    | STM32_I2C_CR2_AUTOEND;
		/* START or repeated start */
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_START;

		for (i = 0; i < in_bytes; i++) {
			/* Wait for receive buffer not empty */
			rv = wait_isr(port, STM32_I2C_ISR_RXNE);
			if (rv)
				goto xfer_exit;

			in[i] = STM32_I2C_RXDR(port);
		}
	}
	rv = wait_isr(port, STM32_I2C_ISR_STOP);
	if (rv)
		goto xfer_exit;

xfer_exit:
	/* clear status */
	STM32_I2C_ICR(port) = 0x3F38;
	/* On error, queue a stop condition */
	if (rv) {
		/* queue a STOP condition */
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_STOP;
		/* wait for it to take effect */
		/* Wait up to 100 us for bus idle */
		for (i = 0; i < 10; i++) {
			if (!(STM32_I2C_ISR(port) & STM32_I2C_ISR_BUSY))
				break;
			udelay(10);
		}

		/*
		 * Allow bus to idle for at least one 100KHz clock = 10 us.
		 * This allows slaves on the bus to detect bus-idle before
		 * the next start condition.
		 */
		udelay(10);
		/* re-initialize the controller */
		STM32_I2C_CR2(port) = 0;
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_PE;
		udelay(10);
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_PE;
	}

	return rv;
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS)
		return gpio_get_level(g);

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS)
		return gpio_get_level(g);

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		(i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
	int len)
{
	int rv;
	uint8_t reg, block_length;

	/*
	 * TODO(crosbug.com/p/23569): when i2c_xfer() supports start/stop bits,
	 * merge this with the LM4 implementation and move to i2c_common.c.
	 */

	if ((len <= 0) || (len > SMBUS_MAX_BLOCK))
		return EC_ERROR_INVAL;

	i2c_lock(port, 1);

	/* Read the counted string into the output buffer */
	reg = offset;
	rv = i2c_xfer(port, slave_addr, &reg, 1, data, len, I2C_XFER_SINGLE);
	if (rv == EC_SUCCESS) {
		/* Block length is the first byte of the returned buffer */
		block_length = MIN(data[0], len - 1);

		/* Move data down, then null-terminate it */
		memmove(data, data + 1, block_length);
		data[block_length] = 0;
	}

	i2c_lock(port, 0);
	return rv;
}

static void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_init_port(p);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);
