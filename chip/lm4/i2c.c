/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "registers.h"
#include "uart.h"
#include "util.h"

#define NUM_PORTS 6

static task_id_t task_waiting_on_port[NUM_PORTS];


static int wait_idle(int port)
{
	int i;
	int wait_msg;

	i = LM4_I2C_MCS(port);
	while (i & 0x01) {
		/* Port is busy, so wait for the interrupt */
		task_waiting_on_port[port] = task_get_current();
		LM4_I2C_MIMR(port) = 0x03;
		wait_msg = task_wait_msg(1000000);
		LM4_I2C_MIMR(port) = 0x00;
		task_waiting_on_port[port] = TASK_ID_INVALID;
		if (wait_msg == 1 << TASK_ID_TIMER)
			return EC_ERROR_TIMEOUT;

		i = LM4_I2C_MCS(port);
	}

	/* Check for errors */
	if (i & 0x02)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}


int i2c_read16(int port, int slave_addr, int offset, int* data)
{
	int rv;
	int d;

	*data = 0;

	/* Transmit the offset address to the slave; leave the master in
	 * transmit state. */
	LM4_I2C_MSA(port) = slave_addr & 0xff;
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
	LM4_I2C_MSA(port) = slave_addr & 0xff;
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

/* TODO:(crosbug.com/p/8026) combine common functions to save space */

int i2c_read8(int port, int slave_addr, int offset, int* data)
{
	int rv;

	*data = 0;

	/* Transmit the offset address to the slave; leave the master in
	 * transmit state. */
	LM4_I2C_MSA(port) = slave_addr & 0xff;
	LM4_I2C_MDR(port) = offset & 0xff;
	LM4_I2C_MCS(port) = 0x03;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Send repeated start followed by receive and stop */
	LM4_I2C_MSA(port) = (slave_addr & 0xff) | 0x01;
	LM4_I2C_MCS(port) = 0x07;	/* NOTE: datasheet suggests 0x0b, but
					 * 0x07 with the change in direction
					 * flips it to a RECEIVE and STOP.
					 * I think.
					 */
	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Read the byte */
	*data = LM4_I2C_MDR(port) & 0xff;

	return EC_SUCCESS;
}

int i2c_write8(int port, int slave_addr, int offset, int data)
{
	int rv;

	/* Transmit the offset address to the slave; leave the master in
	 * transmit state. */
	LM4_I2C_MDR(port) = offset & 0xff;
	LM4_I2C_MSA(port) = slave_addr & 0xff;
	LM4_I2C_MCS(port) = 0x03;

	rv = wait_idle(port);
	if (rv)
		return rv;

	/* Send repeated start followed by transmit and stop */
	LM4_I2C_MDR(port) = data & 0xff;
	LM4_I2C_MCS(port) = 0x05;

	return wait_idle(port);
}

/*****************************************************************************/
/* Interrupt handlers */

/* Handles an interrupt on the specified port. */
static void handle_interrupt(int port)
{
	int id = task_waiting_on_port[port];

	/* Clear the interrupt status*/
	LM4_I2C_MICR(port) = LM4_I2C_MMIS(port);

	/* Wake up the task which was waiting on the interrupt, if any */
	/* TODO: send message based on I2C port number? */
	if (id != TASK_ID_INVALID)
		task_send_msg(id, id, 0);
}


static void i2c0_interrupt(void) { handle_interrupt(0); }
static void i2c1_interrupt(void) { handle_interrupt(1); }
static void i2c2_interrupt(void) { handle_interrupt(2); }
static void i2c3_interrupt(void) { handle_interrupt(3); }
static void i2c4_interrupt(void) { handle_interrupt(4); }
static void i2c5_interrupt(void) { handle_interrupt(5); }

DECLARE_IRQ(LM4_IRQ_I2C0, i2c0_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C1, i2c1_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C2, i2c2_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C3, i2c3_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C4, i2c4_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C5, i2c5_interrupt, 2);

/*****************************************************************************/
/* Console commands */


static void scan_bus(int port, char *desc)
{
	int rv;
	int a;

	uart_printf("Scanning %s I2C bus (%d)...\n", desc, port);

	for (a = 0; a < 0x100; a += 2) {
		uart_puts(".");

		/* Do a single read */
		LM4_I2C_MSA(port) = a | 0x01;
		LM4_I2C_MCS(port) = 0x07;
		rv = wait_idle(port);
		if (rv == EC_SUCCESS)
			uart_printf("\nFound device at 8-bit addr 0x%02x\n", a);
	}
	uart_puts("\n");
}


static int command_i2cread(int argc, char **argv)
{
	int port, addr, count = 1;
	char *e;
	int rv;
	int d, i;

	if (argc < 3) {
		uart_puts("Usage: i2cread <port> <addr> [count]\n");
		return EC_ERROR_UNKNOWN;
	}

	port = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid port\n");
		return EC_ERROR_INVAL;
	}
	if (port != I2C_PORT_THERMAL && port != I2C_PORT_BATTERY &&
	    port != I2C_PORT_CHARGER) {
		uart_puts("Unsupported port\n");
		return EC_ERROR_UNKNOWN;
	}

	addr = strtoi(argv[2], &e, 0);
	if (*e || (addr & 0x01)) {
		uart_puts("Invalid addr; try 'i2cscan' command\n");
		return EC_ERROR_INVAL;
	}

	if (argc > 3) {
		count = strtoi(argv[3], &e, 0);
		if (*e) {
			uart_puts("Invalid count\n");
			return EC_ERROR_INVAL;
		}
	}

	uart_printf("Reading %d bytes from I2C device %d:0x%02x...\n",
		    count, port, addr);
	LM4_I2C_MSA(port) = addr | 0x01;
	for (i = 0; i < count; i++) {
		if (i == 0)
			LM4_I2C_MCS(port) = (count > 1 ? 0x0b : 0x07);
		else
			LM4_I2C_MCS(port) = (i == count - 1 ? 0x05 : 0x09);
		rv = wait_idle(port);
		if (rv != EC_SUCCESS)
			return rv;
		d = LM4_I2C_MDR(port) & 0xff;
		uart_printf("0x%02x ", d);
	}
	uart_puts("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cread, command_i2cread);


static int command_scan(int argc, char **argv)
{
	scan_bus(I2C_PORT_THERMAL, "thermal");
	scan_bus(I2C_PORT_BATTERY, "battery");
	scan_bus(I2C_PORT_CHARGER, "charger");
	uart_puts("done.\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cscan, command_scan);


/*****************************************************************************/
/* Initialization */

/* Configures GPIOs for the module. */
static void configure_gpio(void)
{
#ifdef BOARD_link
	/* PA6:7 = I2C1 SCL/SDA; PB2:3 = I2C0 SCL/SDA; PB6:7 = I2C5 SCL/SDA */
	gpio_set_alternate_function(LM4_GPIO_A, 0xc0, 3);
	gpio_set_alternate_function(LM4_GPIO_B, 0xcc, 3);

	/* Configure SDA as open-drain.  SCL should not be open-drain,
	 * since it has an internal pull-up. */
	LM4_GPIO_ODR(LM4_GPIO_A) |= 0x80;
	LM4_GPIO_ODR(LM4_GPIO_B) |= 0x88;
#else
	/* PG6:7 = I2C5 SCL/SDA */
	gpio_set_alternate_function(LM4_GPIO_G, 0xc0, 3);

	/* Configure SDA as open-drain.  SCL should not be open-drain,
	 * since it has an internal pull-up. */
	LM4_GPIO_ODR(LM4_GPIO_G) |= 0x80;
#endif
}


int i2c_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));
	int i;

	/* Enable I2C modules and delay a few clocks */
	LM4_SYSTEM_RCGCI2C |= (1 << I2C_PORT_THERMAL) |
		(1 << I2C_PORT_BATTERY) | (1 << I2C_PORT_CHARGER);
	scratch = LM4_SYSTEM_RCGCI2C;

	/* Configure GPIOs */
	configure_gpio();

	/* No tasks are waiting on ports */
	for (i = 0; i < NUM_PORTS; i++)
		task_waiting_on_port[i] = TASK_ID_INVALID;

	/* Initialize ports as master, with interrupts enabled */
	LM4_I2C_MCR(I2C_PORT_THERMAL) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_THERMAL) =
		(CPU_CLOCK / (I2C_SPEED_THERMAL * 10 * 2)) - 1;

	LM4_I2C_MCR(I2C_PORT_BATTERY) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_BATTERY) =
		(CPU_CLOCK / (I2C_SPEED_BATTERY * 10 * 2)) - 1;

	LM4_I2C_MCR(I2C_PORT_CHARGER) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_CHARGER) =
		(CPU_CLOCK / (I2C_SPEED_CHARGER * 10 * 2)) - 1;

	LM4_I2C_MCR(I2C_PORT_LIGHTBAR) = 0x10;
	LM4_I2C_MTPR(I2C_PORT_LIGHTBAR) =
		(CPU_CLOCK / (I2C_SPEED_LIGHTBAR * 10 * 2)) - 1;

	/* Enable irqs */
	task_enable_irq(LM4_IRQ_I2C0);
	task_enable_irq(LM4_IRQ_I2C1);
	task_enable_irq(LM4_IRQ_I2C2);
	task_enable_irq(LM4_IRQ_I2C3);
	task_enable_irq(LM4_IRQ_I2C4);
	task_enable_irq(LM4_IRQ_I2C5);

	return EC_SUCCESS;
}
