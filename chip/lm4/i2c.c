/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "board.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "registers.h"
#include "uart.h"
#include "util.h"

/* I2C ports */
#define I2C_PORT_THERMAL 5  // port 5 / PB6:7 on link, but PG6:7 on badger
#define I2C_PORT_BATTERY 5  // port 0 / PB2:3 on Link, open on badger
#define I2C_PORT_CHARGER 5  // port 1 / PA6:7 on Link, user LED on badger
/* I2C port speed in kbps */
#define I2C_SPEED_THERMAL 400  /* TODO: TMP007 supports 3.4Mbps
				  operation; use faster speed? */
#define I2C_SPEED_BATTERY 100
#define I2C_SPEED_CHARGER 100


/* Waits for the I2C bus to go idle */
static int wait_idle(int port)
{
	int s, i;

	/* Spin waiting for busy flag to go away */
	/* TODO: should use interrupt handler */
	for (i = 1000; i >= 0; i--) {
		s = LM4_I2C_MCS(port);
		if (s & 0x01) {
			/* Still busy */
			udelay(1000);
			continue;
		}

		/* Check for errors */
		if (s & 0x02)
			return EC_ERROR_UNKNOWN;

		return EC_SUCCESS;
	}
	return EC_ERROR_TIMEOUT;
}


int i2c_read16(int port, int slave_addr, int offset, int* data)
{
	int rv;
	int d;

	*data = 0;

	/* Transmit the offset address to the slave; leave the master in
	 * transmit state. */
	LM4_I2C_MSA(port) = (slave_addr & 0xff) | 0x00;
	LM4_I2C_MDR(port) = offset & 0xff;
	LM4_I2C_MCS(port) = 0x03;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Send repeated start followed by receive */
	LM4_I2C_MSA(port) = (slave_addr & 0xff) | 0x01;
	LM4_I2C_MCS(port) = 0x0b;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Read the first byte */
	d = LM4_I2C_MDR(port) & 0xff;

	/* Issue another read and then a stop. */
	LM4_I2C_MCS(port) = 0x05;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Read the second byte */
	if (slave_addr & I2C_FLAG_BIG_ENDIAN)
		*data = (d << 8) | (LM4_I2C_MDR(port) & 0xff);
	else
		*data = ((LM4_I2C_MDR(port) & 0xff) << 8) | d;
	return EC_SUCCESS;
}


int i2c_write16(int port, int slave_addr, int offset, int data)
{
	int rv;

	/* Transmit the offset address to the slave; leave the master in
	 * transmit state. */
	LM4_I2C_MDR(port) = offset & 0xff;
	LM4_I2C_MSA(port) = (slave_addr & 0xff) | 0x00;
	LM4_I2C_MCS(port) = 0x03;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Transmit the first byte */
	if (slave_addr & I2C_FLAG_BIG_ENDIAN)
		LM4_I2C_MDR(port) = (data >> 8) & 0xff;
	else
		LM4_I2C_MDR(port) = data & 0xff;
	LM4_I2C_MCS(port) = 0x01;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Transmit the second byte and then a stop */
	if (slave_addr & I2C_FLAG_BIG_ENDIAN)
		LM4_I2C_MDR(port) = data & 0xff;
	else
		LM4_I2C_MDR(port) = (data >> 8) & 0xff;
	LM4_I2C_MCS(port) = 0x05;

	return wait_idle(port);
}


/*****************************************************************************/
/* Console commands */


static void scan_bus(int port, char *desc)
{
	int rv;
	int a;

	uart_printf("Scanning %s I2C bus...\n", desc);

	for (a = 0; a < 0x100; a += 2) {
		uart_puts(".");

		/* Do a single read */
		LM4_I2C_MSA(port) = a | 0x01;
		LM4_I2C_MCS(port) = 0x07;
		rv = wait_idle(port);
		if (rv == EC_SUCCESS)
			uart_printf("\nFound device at 0x%02x\n", a);
	}
	uart_puts("\n");
}


static int command_scan(int argc, char **argv)
{
	scan_bus(I2C_PORT_THERMAL, "thermal");
	scan_bus(I2C_PORT_BATTERY, "battery");
	scan_bus(I2C_PORT_CHARGER, "charger");
	uart_puts("done.\n");
	return EC_SUCCESS;
}


static const struct console_command console_commands[] = {
	{"i2cscan", command_scan},
};
static const struct console_group command_group = {
	"I2C", console_commands, ARRAY_SIZE(console_commands)
};


/*****************************************************************************/
/* Initialization */

/* Configures GPIOs for the module. */
static void configure_gpio(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable GPIOG module and delay a few clocks */
	LM4_SYSTEM_RCGCGPIO |= 0x0040;
	scratch = LM4_SYSTEM_RCGCGPIO;

	/* Use alternate function 3 for PG6:7 */
	LM4_GPIO_AFSEL(G) |= 0xc0;
	LM4_GPIO_PCTL(G) = (LM4_GPIO_PCTL(N) & 0x00ffffff) | 0x33000000;
	LM4_GPIO_DEN(G) |= 0xc0;
	/* Configure SDA as open-drain.  SCL should not be open-drain,
	 * since it has an internal pull-up. */
	LM4_GPIO_ODR(G) |= 0x80;
}


int i2c_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable I2C5 module and delay a few clocks */
	LM4_SYSTEM_RCGCI2C |= (1 << I2C_PORT_THERMAL) |
		(1 << I2C_PORT_BATTERY) | (1 << I2C_PORT_CHARGER);
	scratch = LM4_SYSTEM_RCGCI2C;

	/* Configure GPIOs */
	configure_gpio();

	/* Initialize ports as master */
	LM4_I2C_MCR(I2C_PORT_THERMAL) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_THERMAL) =
		(CPU_CLOCK / (I2C_SPEED_THERMAL * 10 * 2)) - 1;

	LM4_I2C_MCR(I2C_PORT_BATTERY) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_BATTERY) =
		(CPU_CLOCK / (I2C_SPEED_BATTERY * 10 * 2)) - 1;

	LM4_I2C_MCR(I2C_PORT_CHARGER) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_CHARGER) =
		(CPU_CLOCK / (I2C_SPEED_CHARGER * 10 * 2)) - 1;

	console_register_commands(&command_group);
	return EC_SUCCESS;
}
