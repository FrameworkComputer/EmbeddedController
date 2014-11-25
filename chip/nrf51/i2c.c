/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "i2c_arbitration.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

#define I2C_TIMEOUT 20000

static void i2c_init_port(unsigned int port);

/* board-specific setup for post-I2C module init */
void __board_i2c_post_init(int port)
{
}

void board_i2c_post_init(int port)
		__attribute__((weak, alias("__board_i2c_post_init")));

static void i2c_init_port(unsigned int port)
{
	NRF51_TWI_RXDRDY(port) = 0;
	NRF51_TWI_TXDSENT(port) = 0;

	NRF51_TWI_PSELSCL(port) = NRF51_TWI_SCL_PIN(port);
	NRF51_TWI_PSELSDA(port) = NRF51_TWI_SDA_PIN(port);
	NRF51_TWI_FREQUENCY(port) = NRF51_TWI_FREQ(port);

	NRF51_PPI_CHENCLR = 1 << (NRF51_TWI_PPI_CHAN(port));

	NRF51_PPI_EEP(NRF51_TWI_PPI_CHAN(port)) = (uint32_t)&NRF51_TWI_BB(port);
	NRF51_PPI_TEP(NRF51_TWI_PPI_CHAN(port)) =
		(uint32_t)&NRF51_TWI_SUSPEND(port);

	/* Master enable */
	NRF51_TWI_ENABLE(port) = NRF51_TWI_ENABLE_VAL;

	if (!(i2c_raw_get_scl(port) && (i2c_raw_get_sda(port))))
		CPRINTF("port %d could be wedged\n", port);
}

static void i2c_init(void)
{
	int i;

	gpio_config_module(MODULE_I2C, 1);

	for (i = 0; i < i2c_ports_used; i++)
		i2c_init_port(i);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);

static void dump_i2c_reg(int port)
{
#ifdef CONFIG_I2C_DEBUG
	CPRINTF("port      : %01d\n", port);
	CPRINTF("Regs :\n");
	CPRINTF(" 1: INTEN     : %08x\n", NRF51_TWI_INTEN(port));
	CPRINTF(" 2: ERRORSRC  : %08x\n", NRF51_TWI_ERRORSRC(port));
	CPRINTF(" 3: ENABLE    : %08x\n", NRF51_TWI_ENABLE(port));
	CPRINTF(" 4: PSELSCL   : %08x\n", NRF51_TWI_PSELSCL(port));
	CPRINTF(" 5: PSELSDA   : %08x\n", NRF51_TWI_PSELSDA(port));
	CPRINTF(" 6: RXD       : %08x\n", NRF51_TWI_RXD(port));
	CPRINTF(" 7: TXD       : %08x\n", NRF51_TWI_TXD(port));
	CPRINTF(" 8: FREQUENCY : %08x\n", NRF51_TWI_FREQUENCY(port));
	CPRINTF(" 9: ADDRESS   : %08x\n", NRF51_TWI_ADDRESS(port));
	CPRINTF("Events :\n");
	CPRINTF(" STOPPED   : %08x\n", NRF51_TWI_STOPPED(port));
	CPRINTF(" RXDRDY    : %08x\n", NRF51_TWI_RXDRDY(port));
	CPRINTF(" TXDSENT   : %08x\n", NRF51_TWI_TXDSENT(port));
	CPRINTF(" ERROR     : %08x\n", NRF51_TWI_ERROR(port));
	CPRINTF(" BB        : %08x\n", NRF51_TWI_BB(port));
#endif /* CONFIG_I2C_DEBUG */
}

static void i2c_recover(int port)
{
	/*
	 * Recovery of the TWI peripheral:
	 * To recover a TWI peripheral that has been locked up you must use
	 * the following code.
	 * After the recover function it is important to reconfigure all
	 * relevant TWI registers explicitly to ensure that it operates
	 * correctly.
	 *    TWI0:
	 *      NRF_TWI0->ENABLE =
	 *         TWI_ENABLE_ENABLE_Disabled << TWI_ENABLE_ENABLE_Pos;
	 *      *(uint32_t *)(NRF_TWI0_BASE + 0xFFC) = 0;
	 *      nrf_delay_us(5);
	 *      *(uint32_t *)(NRF_TWI0_BASE + 0xFFC) = 1;
	 *      NRF_TWI0->ENABLE =
	 *         TWI_ENABLE_ENABLE_Enabled << TWI_ENABLE_ENABLE_Pos;
	 */
	NRF51_TWI_ENABLE(port) = NRF51_TWI_DISABLE_VAL;
	NRF51_TWI_POWER(port) = 0;
	udelay(5);
	NRF51_TWI_POWER(port) = 1;

	i2c_init_port(port);
}

static void handle_i2c_error(int port, int rv)
{
	if (rv == EC_SUCCESS)
		return;

#ifdef CONFIG_I2C_DEBUG
	if (rv != EC_ERROR_TIMEOUT)
		CPRINTF("handle_i2c_error %d\n", rv);
	else
		CPRINTF("handle_i2c_error: Timeout\n");

	dump_i2c_reg(port);
#endif

	/* This may be a little too heavy handed. */
	i2c_recover(port);
}

static int i2c_master_write(int port, int slave_addr, const uint8_t *data,
	     int size, int stop)
{
	int bytes_sent;
	int timeout = I2C_TIMEOUT;

	NRF51_TWI_ADDRESS(port) = slave_addr >> 1;

	/* Clear the sent bit */
	NRF51_TWI_TXDSENT(port) = 0;

	for (bytes_sent = 0; bytes_sent < size; bytes_sent++) {
		/*Send a byte */
		NRF51_TWI_TXD(port) = data[bytes_sent];

		/* Only send a start for the first byte */
		if (bytes_sent == 0)
			NRF51_TWI_STARTTX(port) = 1;

		/* Wait for ACK/NACK */
		timeout = I2C_TIMEOUT;
		while (timeout > 0 && NRF51_TWI_TXDSENT(port) == 0 &&
				NRF51_TWI_ERROR(port) == 0)
			timeout--;

		if (timeout == 0)
			return EC_ERROR_TIMEOUT;

		if (NRF51_TWI_ERROR(port))
			return EC_ERROR_UNKNOWN;

		/* Clear the sent bit */
		NRF51_TWI_TXDSENT(port) = 0;
	}

	if (stop) {
		NRF51_TWI_STOPPED(port) = 0;
		NRF51_TWI_STOP(port) = 1;
		timeout = 10;
		while (NRF51_TWI_STOPPED(port) == 0 && timeout > 0)
			timeout--;
	}

	return EC_SUCCESS;
}

static int i2c_master_read(int port, int slave_addr, uint8_t *data, int size)
{
	int curr_byte;
	int timeout = I2C_TIMEOUT;

	NRF51_TWI_ADDRESS(port) = slave_addr >> 1;

	if (size == 1) /* Last byte: stop after this one. */
		NRF51_PPI_TEP(NRF51_TWI_PPI_CHAN(port)) =
			(uint32_t)&NRF51_TWI_STOP(port);
	else
		NRF51_PPI_TEP(NRF51_TWI_PPI_CHAN(port)) =
			(uint32_t)&NRF51_TWI_SUSPEND(port);
	NRF51_PPI_CHENSET = 1 << NRF51_TWI_PPI_CHAN(port);

	NRF51_TWI_RXDRDY(port) = 0;
	NRF51_TWI_STARTRX(port) = 1;

	for (curr_byte = 0; curr_byte < size; curr_byte++) {

		/* Wait for data */
		while (timeout > 0 && NRF51_TWI_RXDRDY(port) == 0 &&
				NRF51_TWI_ERROR(port) == 0)
			timeout--;

		if (timeout == 0)
			return EC_ERROR_TIMEOUT;

		if (NRF51_TWI_ERROR(port))
			return EC_ERROR_UNKNOWN;

		data[curr_byte] = NRF51_TWI_RXD(port);
		NRF51_TWI_RXDRDY(port) = 0;

		/* Second to the last byte: stop next time. */
		if (curr_byte == size-2)
			NRF51_PPI_TEP(NRF51_TWI_PPI_CHAN(port)) =
				(uint32_t)&NRF51_TWI_STOP(port);

		/*
		 * According to nRF51822-PAN v2.4 (Product Anomaly Notice),
		 * the I2C locks up when RESUME is triggered too soon.
		 * "the firmware should ensure that the time between receiving
		 * the RXDRDY event and trigging the RESUME task is at least
		 * two times the TWI clock period (i.e. 20 μs at 100 kbps).
		 * Provided the TWI slave doesn’t do clock stretching during
		 * the ACK bit, this will be enough to avoid the RESUME task
		 * hit the end of the ACK bit. If this fails, a recovery of
		 * the peripheral will be necessary, see i2c_recover.
		 */
		udelay(20);
		NRF51_TWI_RESUME(port) = 1;
	}

	timeout = I2C_TIMEOUT;
	while (NRF51_TWI_STOPPED(port) == 0 && timeout > 0)
		timeout--;

	NRF51_TWI_STOP(port) = 0;

	NRF51_PPI_CHENCLR = 1 << NRF51_TWI_PPI_CHAN(port);

	return EC_SUCCESS;
}

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_bytes,
	     uint8_t *in, int in_bytes, int flags)
{
	int rv = EC_SUCCESS;

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	if (out_bytes)
		rv = i2c_master_write(port, slave_addr, out, out_bytes,
				 in_bytes ? 0 : 1);
	if (rv == EC_SUCCESS && in_bytes)
		rv = i2c_master_read(port, slave_addr, in, in_bytes);

	handle_i2c_error(port, rv);

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

	/* If no SDA pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		(i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

