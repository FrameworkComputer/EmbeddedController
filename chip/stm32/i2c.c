/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* 8-bit I2C slave address */
#define I2C_ADDRESS 0x3c

/* I2C bus frequency */
#define I2C_FREQ 100000 /* Hz */

/* Clock divider for I2C controller */
#define I2C_CCR (CPU_CLOCK/(2 * I2C_FREQ))

#define NUM_PORTS 2
#define I2C1      STM32_I2C1_PORT
#define I2C2      STM32_I2C2_PORT


static uint16_t i2c_sr1[NUM_PORTS];

/* buffer for host commands (including error code and checksum) */
static uint8_t host_buffer[EC_PARAM_SIZE + 2];

/* current position in host buffer for reception */
static int rx_index;


static void wait_tx(int port)
{
	/* TODO: Add timeouts and error checking for safety */
	while (!(STM32_I2C_SR1(port) & (1 << 7)))
		;
}

static int i2c_write_raw(int port, void *buf, int len)
{
	int i;
	uint8_t *data = buf;

	for (i = 0; i < len; i++) {
		STM32_I2C_DR(port) = data[i];
		wait_tx(port);
	}

	return len;
}

static void _send_result(int slot, int result, int size)
{
	int i;
	int len = 1;
	uint8_t sum = 0;

	ASSERT(slot == 0);
	/* record the error code */
	host_buffer[0] = result;
	if (size) {
		/* compute checksum */
		for (i = 1; i <= size; i++)
			sum += host_buffer[i];
		host_buffer[size + 1] = sum;
		len = size + 2;
	}

	/* send the answer to the AP */
	i2c_write_raw(I2C2, host_buffer, len);
}

void host_send_result(int slot, int result)
{
	_send_result(slot, result, 0);
}

void host_send_response(int slot, const uint8_t *data, int size)
{
	uint8_t *out = host_get_buffer(slot);

	if (data != out)
		memcpy(out, data, size);

	_send_result(slot, EC_RES_SUCCESS, size);
}

uint8_t *host_get_buffer(int slot)
{
	ASSERT(slot == 0);
	return host_buffer + 1 /* skip room for error code */;
}

static void i2c_event_handler(int port)
{

	/* save and clear status */
	i2c_sr1[port] = STM32_I2C_SR1(port);
	STM32_I2C_SR1(port) = 0;

	/* transfer matched our slave address */
	if (i2c_sr1[port] & (1 << 1)) {
		/* cleared by reading SR1 followed by reading SR2 */
		STM32_I2C_SR1(port);
		STM32_I2C_SR2(port);
	} else if (i2c_sr1[port] & (1 << 4)) {
		/* clear STOPF bit by reading SR1 and then writing CR1 */
		STM32_I2C_SR1(port);
		STM32_I2C_CR1(port) = STM32_I2C_CR1(port);
	}

	/* RxNE event */
	if (i2c_sr1[port] & (1 << 6)) {
		if (port == I2C2) { /* AP issued write command */
			if (rx_index >= sizeof(host_buffer) - 1) {
				rx_index = 0;
				CPRINTF("I2C message too large\n");
			}
			host_buffer[rx_index++] = STM32_I2C_DR(I2C2);
		}
	}
	/* TxE event */
	if (i2c_sr1[port] & (1 << 7)) {
		if (port == I2C2) { /* AP is waiting for EC response */
			if (rx_index) {
				/* we have an available command : execute it */
				host_command_received(0, host_buffer[0]);
				/* reset host buffer after end of transfer */
				rx_index = 0;
			} else {
				/* spurious read : return dummy value */
				STM32_I2C_DR(port) = 0xec;
			}
		}
	}
}
static void i2c2_event_interrupt(void) { i2c_event_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_EV, i2c2_event_interrupt, 3);

static void i2c_error_handler(int port)
{
	i2c_sr1[port] = STM32_I2C_SR1(port);

#ifdef CONFIG_DEBUG
	if (i2c_sr1[port] & 1 << 10) {
		/* ACK failed (NACK); expected when AP reads final byte.
		 * Software must clear AF bit. */
	} else {
		CPRINTF("%s: I2C_SR1(%s): 0x%04x\n",
			__func__, port, i2c_sr1[port]);
		CPRINTF("%s: I2C_SR2(%s): 0x%04x\n",
			__func__, port, STM32_I2C_SR2(port));
	}
#endif

	STM32_I2C_SR1(port) &= ~0xdf00;
}
static void i2c2_error_interrupt(void) { i2c_error_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_ER, i2c2_error_interrupt, 2);

static int i2c_init2(void)
{
	/* enable I2C2 clock */
	STM32_RCC_APB1ENR |= 1 << 22;

	/* force reset if the bus is stuck in BUSY state */
	if (STM32_I2C_SR2(I2C2) & 0x2) {
		STM32_I2C_CR1(I2C2) = 0x8000;
		STM32_I2C_CR1(I2C2) = 0x0000;
	}

	/* set clock configuration : standard mode (100kHz) */
	STM32_I2C_CCR(I2C2) = I2C_CCR;

	/* set slave address */
	STM32_I2C_OAR1(I2C2) = I2C_ADDRESS;

	/* configuration : I2C mode / Periphal enabled, ACK enabled */
	STM32_I2C_CR1(I2C2) = (1 << 10) | (1 << 0);
	/* error and event interrupts enabled / input clock is 16Mhz */
	STM32_I2C_CR2(I2C2) = (1 << 9) | (1 << 8) | 0x10;

	/* clear status */
	STM32_I2C_SR1(I2C2) = 0;

	/* enable event and error interrupts */
	task_enable_irq(STM32_IRQ_I2C2_EV);
	task_enable_irq(STM32_IRQ_I2C2_ER);

	CPUTS("done\n");
	return EC_SUCCESS;
}


static int i2c_init(void)
{
	int rc = 0;

	/* FIXME: Add #defines to determine which channels to init */
	rc |= i2c_init2();
	return rc;
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);
