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

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK 32

/* 8-bit I2C slave address */
#define I2C_ADDRESS 0x3c

/* I2C bus frequency */
#define I2C_FREQ 100000 /* Hz */

/* I2C bit period in microseconds */
#define I2C_PERIOD_US (SECOND / I2C_FREQ)

/* Clock divider for I2C controller */
#define I2C_CCR (CPU_CLOCK / (2 * I2C_FREQ))

/*
 * Transmit timeout in microseconds
 *
 * In theory we shouldn't have a timeout here (at least when we're in slave
 * mode).  The slave is supposed to wait forever for the master to read bytes.
 * ...but we're going to keep the timeout to make sure we're robust.  It may in
 * fact be needed if the host resets itself mid-read.
 *
 * NOTE: One case where this timeout is useful is when the battery
 * flips out.  The battery may flip out and hold lines low for up to
 * 25ms.  If we just wait it will eventually let them go.
 */
#define I2C_TX_TIMEOUT_SLAVE	(100 * MSEC)
#define I2C_TX_TIMEOUT_MASTER	(30 * MSEC)

/*
 * We delay 5us in bitbang mode.  That gives us 5us low and 5us high or
 * a frequency of 100kHz.
 *
 * Note that the code takes a little time to run so we don't actually get
 * 100kHz, but that's OK.
 */
#define I2C_BITBANG_DELAY_US	5

#define I2C1      STM32_I2C1_PORT
#define I2C2      STM32_I2C2_PORT

/* Select the DMA channels matching the board configuration */
#define DMAC_SLAVE_TX \
	((I2C_PORT_SLAVE) ? STM32_DMAC_I2C2_TX : STM32_DMAC_I2C1_TX)
#define DMAC_SLAVE_RX \
	((I2C_PORT_SLAVE) ? STM32_DMAC_I2C2_RX : STM32_DMAC_I2C1_RX)
#define DMAC_MASTER_TX \
	((I2C_PORT_MASTER) ? STM32_DMAC_I2C2_TX : STM32_DMAC_I2C1_TX)
#define DMAC_MASTER_RX \
	((I2C_PORT_MASTER) ? STM32_DMAC_I2C2_RX : STM32_DMAC_I2C1_RX)

enum {
	/*
	 * A stop condition should take 2 clocks, but the process may need more
	 * time to notice if it is preempted, so we poll repeatedly for 8
	 * clocks, before backing off and only check once every
	 * STOP_SENT_RETRY_US for up to TIMEOUT_STOP_SENT clocks before giving
	 * up.
	 */
	SLOW_STOP_SENT_US	= I2C_PERIOD_US * 8,
	TIMEOUT_STOP_SENT_US	= I2C_PERIOD_US * 200,
	STOP_SENT_RETRY_US	= 150,
};

static const struct dma_option dma_tx_option[I2C_PORT_COUNT] = {
	{STM32_DMAC_I2C1_TX, (void *)&STM32_I2C_DR(I2C1),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT},
	{STM32_DMAC_I2C2_TX, (void *)&STM32_I2C_DR(I2C2),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT},
};

static const struct dma_option dma_rx_option[I2C_PORT_COUNT] = {
	{STM32_DMAC_I2C1_RX, (void *)&STM32_I2C_DR(I2C1),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT},
	{STM32_DMAC_I2C2_RX, (void *)&STM32_I2C_DR(I2C2),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT},
};

static uint16_t i2c_sr1[I2C_PORT_COUNT];

/* Buffer for host commands (including version, error code and checksum) */
static uint8_t host_buffer[EC_PROTO2_MAX_REQUEST_SIZE];
static struct host_cmd_handler_args host_cmd_args;
static uint8_t i2c_old_response;  /* Send an old-style response */

/* Flag indicating if a command is currently in the buffer */
static uint8_t rx_pending;

static inline void disable_i2c_interrupt(int port)
{
	STM32_I2C_CR2(port) &= ~(3 << 8);
}

static inline void enable_i2c_interrupt(int port)
{
	STM32_I2C_CR2(port) |= 3 << 8;
}

static inline void enable_ack(int port)
{
	STM32_I2C_CR1(port) |= (1 << 10);
}

static inline void disable_ack(int port)
{
	STM32_I2C_CR1(port) &= ~(1 << 10);
}

static void i2c_init_port(unsigned int port);

static int i2c_write_raw_slave(int port, void *buf, int len)
{
	stm32_dma_chan_t *chan;
	int rv;

	/* we don't want to race with TxE interrupt event */
	disable_i2c_interrupt(port);

	/* Configuring DMA1 channel DMAC_SLAVE_TX */
	enable_ack(port);
	chan = dma_get_channel(DMAC_SLAVE_TX);
	dma_prepare_tx(dma_tx_option + port, len, buf);

	/* Start the DMA */
	dma_go(chan);

	/* Configuring i2c to use DMA */
	STM32_I2C_CR2(port) |= (1 << 11);

	if (in_interrupt_context()) {
		/* Poll for the transmission complete flag */
		dma_wait(DMAC_SLAVE_TX);
		dma_clear_isr(DMAC_SLAVE_TX);
	} else {
		/* Wait for the transmission complete Interrupt */
		dma_enable_tc_interrupt(DMAC_SLAVE_TX);
		rv = task_wait_event(DMA_TRANSFER_TIMEOUT_US);
		dma_disable_tc_interrupt(DMAC_SLAVE_TX);

		if (!(rv & TASK_EVENT_WAKE)) {
			CPRINTF("[%T Slave timeout, resetting i2c]\n");
			i2c_init_port(port);
		}
	}

	dma_disable(DMAC_SLAVE_TX);
	STM32_I2C_CR2(port) &= ~(1 << 11);

	enable_i2c_interrupt(port);

	return len;
}

static void i2c_send_response(struct host_cmd_handler_args *args)
{
	const uint8_t *data = args->response;
	int size = args->response_size;
	uint8_t *out = host_buffer;
	int sum = 0, i;

	*out++ = args->result;
	if (!i2c_old_response) {
		*out++ = size;
		sum = args->result + size;
	}
	for (i = 0; i < size; i++, data++, out++) {
		if (data != out)
			*out = *data;
		sum += *data;
	}
	*out++ = sum & 0xff;

	/* send the answer to the AP */
	i2c_write_raw_slave(I2C2, host_buffer, out - host_buffer);
}

/* Process the command in the i2c host buffer */
static void i2c_process_command(void)
{
	struct host_cmd_handler_args *args = &host_cmd_args;
	char *buff = host_buffer;

	args->command = *buff;
	args->result = EC_RES_SUCCESS;
	if (args->command >= EC_CMD_VERSION0) {
		int csum, i;

		/* Read version and data size */
		args->version = args->command - EC_CMD_VERSION0;
		args->command = buff[1];
		args->params_size = buff[2];

		/* Verify checksum */
		for (csum = i = 0; i < args->params_size + 3; i++)
			csum += buff[i];
		if ((uint8_t)csum != buff[i])
			args->result = EC_RES_INVALID_CHECKSUM;

		buff += 3;
		i2c_old_response = 0;
	} else {
		/*
		 * Old style (version 1) command.
		 *
		 * TODO(crosbug.com/p/23765): Nothing sends these anymore,
		 * since this was superseded by version 2 before snow launched.
		 * This code should be safe to remove.
		 */
		args->version = 0;
		args->params_size = EC_PROTO2_MAX_PARAM_SIZE;	/* unknown */
		buff++;
		i2c_old_response = 1;
	}

	/* we have an available command : execute it */
	args->send_response = i2c_send_response;
	args->params = buff;
	/* skip room for error code, arglen */
	args->response = host_buffer + 2;
	args->response_max = EC_PROTO2_MAX_PARAM_SIZE;
	args->response_size = 0;

	host_command_received(args);
}

static void i2c_event_handler(int port)
{
	/* save and clear status */
	i2c_sr1[port] = STM32_I2C_SR1(port);
	STM32_I2C_SR1(port) = 0;

	/* Confirm that you are not in master mode */
	if (STM32_I2C_SR2(port) & (1 << 0)) {
		CPRINTF("I2C slave ISR triggered in master mode, ignoring.\n");
		return;
	}

	/* transfer matched our slave address */
	if (i2c_sr1[port] & (1 << 1)) {
		/* If it's a receiver slave */
		if (!(STM32_I2C_SR2(port) & (1 << 2))) {
			dma_start_rx(dma_rx_option + port, sizeof(host_buffer),
				     host_buffer);

			STM32_I2C_CR2(port) |= (1 << 11);
			rx_pending = 1;
		}

		/* cleared by reading SR1 followed by reading SR2 */
		STM32_I2C_SR1(port);
		STM32_I2C_SR2(port);
	} else if (i2c_sr1[port] & (1 << 4)) {
		/* If it's a receiver slave */
		if (!(STM32_I2C_SR2(port) & (1 << 2))) {
			/* Disable, and clear the DMA transfer complete flag */
			dma_disable(DMAC_SLAVE_RX);
			dma_clear_isr(DMAC_SLAVE_RX);

			/* Turn off i2c's DMA flag */
			STM32_I2C_CR2(port) &= ~(1 << 11);
		}
		/* clear STOPF bit by reading SR1 and then writing CR1 */
		STM32_I2C_SR1(port);
		STM32_I2C_CR1(port) = STM32_I2C_CR1(port);
	}

	/* TxE event */
	if (i2c_sr1[port] & (1 << 7)) {
		if (port == I2C2) { /* AP is waiting for EC response */
			if (rx_pending) {
				i2c_process_command();
				/* reset host buffer after end of transfer */
				rx_pending = 0;
			} else {
				/* spurious read : return dummy value */
				STM32_I2C_DR(port) = 0xec;
			}
		}
	}
}
void i2c2_event_interrupt(void) { i2c_event_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_EV, i2c2_event_interrupt, 3);

static void i2c_error_handler(int port)
{
	i2c_sr1[port] = STM32_I2C_SR1(port);

	if (i2c_sr1[port] & 1 << 10) {
		/* ACK failed (NACK); expected when AP reads final byte.
		 * Software must clear AF bit. */
	} else {
		CPRINTF("%s: I2C_SR1(%d): 0x%04x\n",
			__func__, port, i2c_sr1[port]);
		CPRINTF("%s: I2C_SR2(%d): 0x%04x\n",
			__func__, port, STM32_I2C_SR2(port));
	}

	STM32_I2C_SR1(port) &= ~0xdf00;
}
void i2c2_error_interrupt(void) { i2c_error_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_ER, i2c2_error_interrupt, 2);

/* board-specific setup for post-I2C module init */
void __board_i2c_post_init(int port)
{
}

void board_i2c_post_init(int port)
		__attribute__((weak, alias("__board_i2c_post_init")));

static void i2c_init_port(unsigned int port)
{
	const int i2c_clock_bit[] = {21, 22};

	if (!(STM32_RCC_APB1ENR & (1 << i2c_clock_bit[port]))) {
		/* Only unwedge the bus if the clock is off */
		if (i2c_claim(port) == EC_SUCCESS) {
			i2c_release(port);
		}

		/* enable I2C2 clock */
		STM32_RCC_APB1ENR |= 1 << i2c_clock_bit[port];
	}

	/* force reset of the i2c peripheral */
	STM32_I2C_CR1(port) = 0x8000;
	STM32_I2C_CR1(port) = 0x0000;

	/* set clock configuration : standard mode (100kHz) */
	STM32_I2C_CCR(port) = I2C_CCR;

	/* set slave address */
	if (port == I2C2)
		STM32_I2C_OAR1(port) = I2C_ADDRESS;

	/* configuration : I2C mode / Periphal enabled, ACK enabled */
	STM32_I2C_CR1(port) = (1 << 10) | (1 << 0);
	/* error and event interrupts enabled / input clock is 16Mhz */
	STM32_I2C_CR2(port) = (1 << 9) | (1 << 8) | 0x10;

	/* clear status */
	STM32_I2C_SR1(port) = 0;

	board_i2c_post_init(port);
}

static void i2c_init(void)
{
	/*
	 * TODO(crosbug.com/p/23763): Add config options to determine which
	 * channels to init.
	 */
	i2c_init_port(I2C1);
	i2c_init_port(I2C2);

	/* Enable event and error interrupts */
	task_enable_irq(STM32_IRQ_I2C2_EV);
	task_enable_irq(STM32_IRQ_I2C2_ER);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* STM32 Host I2C */

#define SR1_SB		(1 << 0)	/* Start bit sent */
#define SR1_ADDR	(1 << 1)	/* Address sent */
#define SR1_BTF		(1 << 2)	/* Byte transfered */
#define SR1_ADD10	(1 << 3)	/* 10bit address sent */
#define SR1_STOPF	(1 << 4)	/* Stop detected */
#define SR1_RxNE	(1 << 6)	/* Data reg not empty */
#define SR1_TxE		(1 << 7)	/* Data reg empty */
#define SR1_BERR	(1 << 8)	/* Buss error */
#define SR1_ARLO	(1 << 9)	/* Arbitration lost */
#define SR1_AF		(1 << 10)	/* Ack failure */
#define SR1_OVR		(1 << 11)	/* Overrun/underrun */
#define SR1_PECERR	(1 << 12)	/* PEC err in reception */
#define SR1_TIMEOUT	(1 << 14)	/* Timeout : 25ms */
#define CR2_DMAEN	(1 << 11)	/* DMA enable */
#define CR2_LAST	(1 << 12)	/* Next EOT is last EOT */

static inline void dump_i2c_reg(int port)
{
#ifdef CONFIG_I2C_DEBUG
	CPRINTF("CR1  : %016b\n", STM32_I2C_CR1(port));
	CPRINTF("CR2  : %016b\n", STM32_I2C_CR2(port));
	CPRINTF("SR2  : %016b\n", STM32_I2C_SR2(port));
	CPRINTF("SR1  : %016b\n", STM32_I2C_SR1(port));
	CPRINTF("OAR1 : %016b\n", STM32_I2C_OAR1(port));
	CPRINTF("OAR2 : %016b\n", STM32_I2C_OAR2(port));
	CPRINTF("DR   : %016b\n", STM32_I2C_DR(port));
	CPRINTF("CCR  : %016b\n", STM32_I2C_CCR(port));
	CPRINTF("TRISE: %016b\n", STM32_I2C_TRISE(port));
#endif /* CONFIG_I2C_DEBUG */
}

enum wait_t {
	WAIT_NONE,
	WAIT_MASTER_START,
	WAIT_ADDR_READY,
	WAIT_XMIT_TXE,
	WAIT_XMIT_FINAL_TXE,
	WAIT_XMIT_BTF,
	WAIT_XMIT_STOP,
	WAIT_RX_NE,
	WAIT_RX_NE_FINAL,
	WAIT_RX_NE_STOP,
	WAIT_RX_NE_STOP_SIZE2,
};

/**
 * Wait for a specific i2c event
 *
 * This function waits until the bit(s) corresponding to mask in
 * the specified port's I2C SR1 register is/are set.  It may
 * return a timeout or success.
 *
 * @param port Port to wait on
 * @param mask A mask specifying which bits in SR1 to wait to be set
 * @param wait A wait code to be returned with the timeout error code if that
 *             occurs, to help with debugging.
 * @return EC_SUCCESS, or EC_ERROR_TIMEOUT with the wait code OR'd onto the
 *             bits 8-16 to indicate what it timed out waiting for.
 */
static int wait_status(int port, uint32_t mask, enum wait_t wait)
{
	uint32_t r;
	timestamp_t t1, t2;

	t1 = t2 = get_time();
	r = STM32_I2C_SR1(port);
	while (mask ? ((r & mask) != mask) : r) {
		t2 = get_time();

		if (t2.val - t1.val > I2C_TX_TIMEOUT_MASTER)
			return EC_ERROR_TIMEOUT | (wait << 8);
		else if (t2.val - t1.val > 150)
			usleep(100);

		r = STM32_I2C_SR1(port);
	}

	return EC_SUCCESS;
}

static inline uint32_t read_clear_status(int port)
{
	uint32_t sr1, sr2;

	sr1 = STM32_I2C_SR1(port);
	sr2 = STM32_I2C_SR2(port);
	return (sr2 << 16) | (sr1 & 0xffff);
}

static int master_start(int port, int slave_addr)
{
	int rv;

	/* Change to master send mode, reset stop bit, send start bit */
	STM32_I2C_CR1(port) = (STM32_I2C_CR1(port) & ~(1 << 9)) | (1 << 8);
	/* Wait for start bit sent event */
	rv = wait_status(port, SR1_SB, WAIT_MASTER_START);
	if (rv)
		return rv;

	/* Send address */
	STM32_I2C_DR(port) = slave_addr;
	/* Wait for addr ready */
	rv = wait_status(port, SR1_ADDR, WAIT_ADDR_READY);
	if (rv)
		return rv;

	read_clear_status(port);

	return EC_SUCCESS;
}

static void master_stop(int port)
{
	STM32_I2C_CR1(port) |= (1 << 9);
}

static int wait_until_stop_sent(int port)
{
	timestamp_t deadline;
	timestamp_t slow_cutoff;
	uint8_t is_slow;

	deadline = slow_cutoff = get_time();
	deadline.val += TIMEOUT_STOP_SENT_US;
	slow_cutoff.val += SLOW_STOP_SENT_US;

	while (STM32_I2C_CR1(port) & (1 << 9)) {
		if (timestamp_expired(deadline, NULL)) {
			ccprintf("Stop event deadline passed:\ttask=%d"
							"\tCR1=%016b\n",
				(int)task_get_current(), STM32_I2C_CR1(port));
			return EC_ERROR_TIMEOUT;
		}

		if (is_slow) {
			/* If we haven't gotten a fast response, sleep */
			usleep(STOP_SENT_RETRY_US);
		} else {
			/* Check to see if this request is taking a while */
			if (timestamp_expired(slow_cutoff, NULL)) {
				ccprintf("Stop event taking a while: task=%d",
					(int)task_get_current());
				is_slow = 1;
			}
		}
	}

	return EC_SUCCESS;
}

static void handle_i2c_error(int port, int rv)
{
	timestamp_t t1, t2;
	uint32_t r;

	/* We have not used the bus, just exit */
	if (rv == EC_ERROR_BUSY)
		return;

	/* EC_ERROR_TIMEOUT may have a code specifying where the timeout was */
	if ((rv & 0xff) == EC_ERROR_TIMEOUT) {
#ifdef CONFIG_I2C_DEBUG
		CPRINTF("Wait_status() timeout type: %d\n", (rv >> 8));
#endif
		rv = EC_ERROR_TIMEOUT;
	}
	if (rv)
		dump_i2c_reg(port);

	/* Clear rc_w0 bits */
	STM32_I2C_SR1(port) = 0;
	/* Clear seq read status bits */
	r = STM32_I2C_SR1(port);
	r = STM32_I2C_SR2(port);
	/* Clear busy state */
	t1 = get_time();

	if (rv == EC_ERROR_TIMEOUT && (STM32_I2C_CR1(port) & (1 << 8))) {
		/*
		 * If it failed while just trying to send the start bit then
		 * something is wrong with the internal state of the i2c,
		 * (Probably a stray pulse on the line got it out of sync with
		 * the actual bytes) so reset it.
		 */
		CPRINTF("Unable to send START, resetting i2c.\n");
		i2c_init_port(port);
		goto cr_cleanup;
	} else if (rv == EC_ERROR_TIMEOUT && !(r & 2)) {
		/*
		 * If the BUSY bit is faulty, send a stop bit just to be sure.
		 * It seems that this can be happen very briefly while sending
		 * a 1. We've not actually seen this, but just to be safe.
		 */
		CPRINTF("Bad BUSY bit detected.\n");
		master_stop(port);
	}

	/* Try to send stop bits until the bus becomes idle */
	while (r & 2) {
		t2 = get_time();
		if (t2.val - t1.val > I2C_TX_TIMEOUT_MASTER) {
			dump_i2c_reg(port);
			/* Reset the i2c periph to get it back to slave mode */
			i2c_init_port(port);
			goto cr_cleanup;
		}
		/* Send stop */
		master_stop(port);
		usleep(1000);
		r = STM32_I2C_SR2(port);
	}

cr_cleanup:
	/*
	 * Reset control register to the default state :
	 * I2C mode / Periphal enabled, ACK enabled
	 */
	STM32_I2C_CR1(port) = (1 << 10) | (1 << 0);
}

static int i2c_master_transmit(int port, int slave_addr, const uint8_t *data,
			       int size, int stop)
{
	int rv, rv_start;

	disable_ack(port);

	/* Configure DMA channel for TX to host */
	dma_prepare_tx(dma_tx_option + port, size, data);
	dma_enable_tc_interrupt(DMAC_MASTER_TX);

	/* Start the DMA */
	dma_go(dma_get_channel(DMAC_MASTER_TX));

	/* Configuring i2c2 to use DMA */
	STM32_I2C_CR2(port) |= CR2_DMAEN;

	/* Initialise i2c communication by sending START and ADDR */
	rv_start = master_start(port, slave_addr);

	/* If it started, wait for the transmission complete Interrupt */
	if (!rv_start)
		rv = task_wait_event(DMA_TRANSFER_TIMEOUT_US);

	dma_disable(DMAC_MASTER_TX);
	dma_disable_tc_interrupt(DMAC_MASTER_TX);
	STM32_I2C_CR2(port) &= ~CR2_DMAEN;

	if (rv_start)
		return rv_start;
	if (!(rv & TASK_EVENT_WAKE))
		return EC_ERROR_TIMEOUT;

	rv = wait_status(port, SR1_BTF, WAIT_XMIT_BTF);
	if (rv)
		return rv;

	if (stop) {
		master_stop(port);
		return wait_status(port, 0, WAIT_XMIT_STOP);
	}

	return EC_SUCCESS;
}

static int i2c_master_receive(int port, int slave_addr, uint8_t *data,
			      int size)
{
	int rv, rv_start;

	if (data == NULL || size < 1)
		return EC_ERROR_INVAL;

	/* Master receive only supports DMA for payloads > 1 byte */
	if (size > 1) {
		enable_ack(port);
		dma_start_rx(dma_rx_option + port, size, data);

		dma_enable_tc_interrupt(DMAC_MASTER_RX);

		STM32_I2C_CR2(port) |= CR2_DMAEN;
		STM32_I2C_CR2(port) |= CR2_LAST;

		rv_start = master_start(port, slave_addr | 1);
		if (!rv_start)
			rv = task_wait_event(DMA_TRANSFER_TIMEOUT_US);

		dma_disable(DMAC_MASTER_RX);
		dma_disable_tc_interrupt(DMAC_MASTER_RX);
		STM32_I2C_CR2(port) &= ~CR2_DMAEN;
		disable_ack(port);

		if (rv_start)
			return rv_start;
		if (!(rv & TASK_EVENT_WAKE))
			return EC_ERROR_TIMEOUT;

		master_stop(port);
	} else {
		disable_ack(port);

		rv = master_start(port, slave_addr | 1);
		if (rv)
			return rv;
		master_stop(port);
		rv = wait_status(port, SR1_RxNE, WAIT_RX_NE_STOP_SIZE2);
		if (rv)
			return rv;
		data[0] = STM32_I2C_DR(port);
	}

	return wait_until_stop_sent(port);
}

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_bytes,
	     uint8_t *in, int in_bytes, int flags)
{
	int rv;

	/* TODO(crosbug.com/p/23569): support start/stop flags */

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	if (i2c_claim(port))
		return EC_ERROR_BUSY;

	/* If the port appears to be wedged, then try to unwedge it. */
	if (!i2c_raw_get_scl(port) || !i2c_raw_get_sda(port)) {
		i2c_unwedge(port);

		/* Reset the i2c port. */
		i2c_init_port(port);
	}

	disable_i2c_interrupt(port);

	rv = i2c_master_transmit(port, slave_addr, out, out_bytes,
				 in_bytes ? 0 : 1);
	if (!rv && in_bytes)
		rv = i2c_master_receive(port, slave_addr, in, in_bytes);
	handle_i2c_error(port, rv);

	enable_i2c_interrupt(port);

	i2c_release(port);

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
