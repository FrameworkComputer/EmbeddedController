/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "message.h"
#include "registers.h"
#include "task.h"
#include "uart.h"

/* 8-bit I2C slave address */
#define I2C_ADDRESS 0xec

/* I2C bus frequency */
#define I2C_FREQ 100000 /* Hz */

/* Clock divider for I2C controller */
#define I2C_CCR (CPU_CLOCK/(2 * I2C_FREQ))

#define NUM_PORTS 2
#define I2C1      STM32L_I2C1_PORT
#define I2C2      STM32L_I2C2_PORT

static task_id_t task_waiting_on_port[NUM_PORTS];
static struct mutex port_mutex[NUM_PORTS];

static uint16_t i2c_sr1[NUM_PORTS];

/* per-transaction counters */
static unsigned int tx_byte_count;
static unsigned int rx_byte_count;

/* i2c_xmit_mode determines what EC sends when AP initiates a
   read transaction */
static enum message_cmd_t i2c_xmit_mode[NUM_PORTS];

/*
 * Our output buffers. These must be large enough for our largest message,
 * including protocol overhead.
 */
static uint8_t out_msg[32];


static void wait_rx(int port)
{
	/* TODO: Add timeouts and error checking for safety */
	while (!(STM32L_I2C_SR1(port) & (1 << 6)))
		;
}

static void wait_tx(int port)
{
	/* TODO: Add timeouts and error checking for safety */
	while (!(STM32L_I2C_SR1(port) & (1 << 7)))
		;
}

static int i2c_read_raw(int port, void *buf, int len)
{
	int i;
	uint8_t *data = buf;

	mutex_lock(&port_mutex[port]);
	rx_byte_count = 0;
	for (i = 0; i < len; i++) {
		wait_rx(port);
		data[i] = STM32L_I2C_DR(port);
		rx_byte_count++;
	}
	mutex_unlock(&port_mutex[port]);

	return len;
}

static int i2c_write_raw(int port, void *buf, int len)
{
	int i;
	uint8_t *data = buf;

	mutex_lock(&port_mutex[port]);
	tx_byte_count = 0;
	for (i = 0; i < len; i++) {
		tx_byte_count++;
		STM32L_I2C_DR(port) = data[i];
		wait_tx(port);
	}
	mutex_unlock(&port_mutex[port]);

	return len;
}

void i2c2_work_task(void)
{
	int msg_len;
	uint16_t tmp16;
	task_waiting_on_port[1] = task_get_current();

	while (1) {
		task_wait_event(-1);
		tmp16 = i2c_sr1[I2C2];
		if (tmp16 & (1 << 6)) {
			/* RxNE; AP issued write command */
			i2c_read_raw(I2C2, &i2c_xmit_mode[I2C2], 1);
#ifdef CONFIG_DEBUG
			uart_printf("%s: i2c2_xmit_mode: %02x\n",
					__func__, i2c_xmit_mode[I2C2]);
#endif
		} else if (tmp16 & (1 << 7)) {
			/* RxE; AP is waiting for EC response */
			msg_len = message_process_cmd(i2c_xmit_mode[I2C2],
						  out_msg, sizeof(out_msg));
			if (msg_len > 0) {
				i2c_write_raw(I2C2, out_msg, msg_len);
			} else {
				uart_printf("%s: unexpected mode %u\n",
						__func__, i2c_xmit_mode[I2C2]);
			}
		}
	}
}

static void i2c_event_handler(int port)
{

	/* save and clear status */
	i2c_sr1[port] = STM32L_I2C_SR1(port);
	STM32L_I2C_SR1(port) = 0;

	/* transfer matched our slave address */
	if (i2c_sr1[port] & (1 << 1)) {
		/* cleared by reading SR1 followed by reading SR2 */
		STM32L_I2C_SR1(port);
		STM32L_I2C_SR2(port);
#ifdef CONFIG_DEBUG
		uart_printf("%s: ADDR\n", __func__);
#endif
	} else if (i2c_sr1[port] & (1 << 2)) {
		;
#ifdef CONFIG_DEBUG
		uart_printf("%s: BTF\n", __func__);
#endif
	} else if (i2c_sr1[port] & (1 << 4)) {
		/* clear STOPF bit by reading SR1 and then writing CR1 */
		STM32L_I2C_SR1(port);
		STM32L_I2C_CR1(port) = STM32L_I2C_CR1(port);
#ifdef CONFIG_DEBUG
		uart_printf("%s: STOPF\n", __func__);
#endif
	} else {
		;
#ifdef CONFIG_DEBUG
		uart_printf("%s: unknown event\n", __func__);
#endif
	}

	/* RxNE or TxE, wake the worker task */
	if (i2c_sr1[port] & ((1 << 6) | (1 << 7))) {
		if (port == I2C2)
			task_wake(TASK_ID_I2C2_WORK);
	}
}
static void i2c2_event_interrupt(void) { i2c_event_handler(I2C2); }
DECLARE_IRQ(STM32L_IRQ_I2C2_EV, i2c2_event_interrupt, 3);

static void i2c_error_handler(int port)
{
	i2c_sr1[port] = STM32L_I2C_SR1(port);

#ifdef CONFIG_DEBUG
	if (i2c_sr1[port] & 1 << 10) {
		/* ACK failed (NACK); expected when AP reads final byte.
		 * Software must clear AF bit. */
		uart_printf("%s: AF detected\n", __func__);
	}
	uart_printf("%s: tx byte count: %u, rx_byte_count: %u\n",
			__func__, tx_byte_count, rx_byte_count);
	uart_printf("%s: I2C_SR1(%s): 0x%04x\n", __func__, port, i2c_sr1[port]);
	uart_printf("%s: I2C_SR2(%s): 0x%04x\n",
			__func__, port, STM32L_I2C_SR2(port));
#endif

	STM32L_I2C_SR1(port) &= ~0xdf00;
}
static void i2c2_error_interrupt(void) { i2c_error_handler(I2C2); }
DECLARE_IRQ(STM32L_IRQ_I2C2_ER, i2c2_error_interrupt, 2);

static int i2c_init2(void)
{
	int i;

	/* enable I2C2 clock */
	STM32L_RCC_APB1ENR |= 1 << 22;

	/* set clock configuration : standard mode (100kHz) */
	STM32L_I2C_CCR(I2C2) = I2C_CCR;

	/* set slave address */
	STM32L_I2C_OAR1(I2C2) = I2C_ADDRESS;

	/* configuration : I2C mode / Periphal enabled, ACK enabled */
	STM32L_I2C_CR1(I2C2) = (1 << 10) | (1 << 0);
	/* error and event interrupts enabled / input clock is 16Mhz */
	STM32L_I2C_CR2(I2C2) = (1 << 9) | (1 << 8) | 0x10;

	/* clear status */
	STM32L_I2C_SR1(I2C2) = 0;

	/* No tasks are waiting on ports */
	for (i = 0; i < NUM_PORTS; i++)
		task_waiting_on_port[i] = TASK_ID_INVALID;

	/* enable event and error interrupts */
	task_enable_irq(STM32L_IRQ_I2C2_EV);
	task_enable_irq(STM32L_IRQ_I2C2_ER);

	uart_printf("done\n");
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
