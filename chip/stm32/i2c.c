/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

#ifdef DEBUG
#define debug(f, a...) CPRINTF(f, ##a)
#else
#define debug(f, a...)
#endif

/* 8-bit I2C slave address */
#define I2C_ADDRESS 0x3c

/* I2C bus frequency */
#define I2C_FREQ 100000 /* Hz */

/* I2C bit period in microseconds */
#define I2C_PERIOD_US (1000000 / I2C_FREQ)

/* Clock divider for I2C controller */
#define I2C_CCR (CPU_CLOCK/(2 * I2C_FREQ))

/*
 * Transmit timeout in microseconds
 *
 * In theory we shouldn't have a timeout here (at least when we're in slave
 * mode).  The slave is supposed to wait forever for the master to read bytes.
 * ...but we're going to keep the timeout to make sure we're robust.  It may in
 * fact be needed if the host resets itself mid-read.
 */
#define I2C_TX_TIMEOUT_SLAVE	100000 /* us */
#define I2C_TX_TIMEOUT_MASTER	10000  /* us */

#define NUM_PORTS 2
#define I2C1      STM32_I2C1_PORT
#define I2C2      STM32_I2C2_PORT

enum {
	/*
	 * A stop condition should take 2 clocks, but the process may need more
	 * time to notice if it is preempted, so we poll repeatedly for 8
	 * clocks, before backing off and only check once every
	 * STOP_SENT_RETRY_US for up to TIMEOUT_STOP_SENT clocks before
	 * giving up.
	 */
	SLOW_STOP_SENT_US	= I2C_PERIOD_US * 8,
	TIMEOUT_STOP_SENT_US	= I2C_PERIOD_US * 200,
	STOP_SENT_RETRY_US	= 150,
};

static uint16_t i2c_sr1[NUM_PORTS];
static struct mutex i2c_mutex;

/* buffer for host commands (including version, error code and checksum) */
static uint8_t host_buffer[EC_HOST_PARAM_SIZE + 4];
static struct host_cmd_handler_args host_cmd_args;

/* Flag indicating if a command is currently in the buffer */
static uint8_t rx_pending;

/* Indicates that a command is in progress */
static uint8_t command_pending;

/* The result of the last 'slow' operation */
static uint8_t saved_result = EC_RES_UNAVAILABLE;

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

int __board_i2c_claim(int port)
{
	return 0;
}

int board_i2c_claim(int port)
	__attribute__((weak, alias("__board_i2c_claim")));


void __board_i2c_release(int port)
{
}

void board_i2c_release(int port)
	__attribute__((weak, alias("__board_i2c_release")));

static int i2c_write_raw_slave(int port, void *buf, int len)
{
	struct dma_channel *chan;

	/* we don't want to race with TxE interrupt event */
	disable_i2c_interrupt(port);

	/* Configuring DMA1 channel DMAC_I2X_TX */
	enable_ack(port);
	chan = dma_get_channel(DMAC_I2C_TX);
	dma_prepare_tx(chan, len, (void *)&STM32_I2C_DR(port), buf);

	/* Start the DMA */
	dma_go(chan);

	/* Configuring i2c2 to use DMA */
	STM32_I2C_CR2(port) |= (1 << 11);

	if (in_interrupt_context()) {
		/* Poll for the transmission complete flag */
		dma_wait(DMAC_I2C_TX);
		dma_clear_isr(DMAC_I2C_TX);
	} else {
		/* Wait for the transmission complete Interrupt */
		dma_enable_tc_interrupt(DMAC_I2C_TX);
		task_wait_event(DMA_TRANSFER_TIMEOUT_US);
		dma_disable_tc_interrupt(DMAC_I2C_TX);
	}

	dma_disable(DMAC_I2C_TX);
	STM32_I2C_CR2(port) &= ~(1 << 11);

	enable_i2c_interrupt(port);

	return len;
}

static void i2c_send_response(struct host_cmd_handler_args *args)
{
	const uint8_t *data = args->response;
	int size = args->response_size;
	uint8_t *out = host_buffer;
	int watch_command_pending = !in_interrupt_context();
	int sum = 0, i;

	/*
	 * TODO(sjg@chromium.org):
	 * The logic here is a little painful since we are avoiding changing
	 * host_command. If we got an 'in progress' previously, then this
	 * must be the completion of that command, so stash the result
	 * code. We can't send it back to the host now since we already sent
	 * the in-progress response and the host is on to other things now.
	 *
	 * Of course, if we are in interrupt context, then we are just
	 * handling a get_status response. We can't check that in
	 * args->command of course because the original command value has
	 * now been overwritten. This would be much easier to do in
	 * host_command since it actually knows what is going on.
	 *
	 * When a EC_CMD_RESEND_RESPONSE arrives we will supply this response
	 * to that command.
	 *
	 * We don't support stashing response data, so mark the response as
	 * unavailable in that case.
	 *
	 * TODO(sjg@chromium): It would all be easier if drivers used a
	 * stack variable for args and host_command was responsible for
	 * saving the command before execution. Perhaps fast commands could
	 * be executed in interrupt context anyway?
	 */
	if (command_pending && watch_command_pending) {
		debug("pending complete, size=%d, result=%d\n",
			args->response_size, args->result);
		if (args->response_size != 0)
			saved_result = EC_RES_UNAVAILABLE;
		else
			saved_result = args->result;
		command_pending = 0;
		return;
	}

	*out++ = args->result;
	if (!args->i2c_old_response) {
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

	if (watch_command_pending) {
		command_pending = (args->result == EC_RES_IN_PROGRESS);
		if (command_pending)
			debug("Command pending\n");
	}
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
		args->i2c_old_response = 0;
	} else {
		/* Old style command */
		args->version = 0;
		args->params_size = EC_HOST_PARAM_SIZE;	/* unknown */
		buff++;
		args->i2c_old_response = 1;
	}

	/* we have an available command : execute it */
	args->send_response = i2c_send_response;
	args->params = buff;
	/* skip room for error code, arglen */
	args->response = host_buffer + 2;
	args->response_max = EC_HOST_PARAM_SIZE;
	args->response_size = 0;

	/*
	 * Special handling for GET_STATUS, which happens entirely outside
	 * host_command.
	 */
	if (args->command == EC_CMD_GET_COMMS_STATUS) {
		/*
		 * Could do this directly, but then we get no logging
		 * args->result = host_command_get_comms_status(args);
		 */
		args->result = host_command_process(args);
		args->send_response(args);
	} else {
		host_command_received(args);
	}
}

static void i2c_event_handler(int port)
{

	/* save and clear status */
	i2c_sr1[port] = STM32_I2C_SR1(port);
	STM32_I2C_SR1(port) = 0;

	/* transfer matched our slave address */
	if (i2c_sr1[port] & (1 << 1)) {
		/* If it's a receiver slave */
		if (!(STM32_I2C_SR2(port) & (1 << 2))) {
			dma_start_rx(DMAC_I2C_RX, sizeof(host_buffer),
				(void *)&STM32_I2C_DR(port), host_buffer);

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
			dma_disable(DMAC_I2C_RX);
			dma_clear_isr(DMAC_I2C_RX);

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
static void i2c2_event_interrupt(void) { i2c_event_handler(I2C2); }
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
static void i2c2_error_interrupt(void) { i2c_error_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_ER, i2c2_error_interrupt, 2);

/* board-specific setup for post-I2C module init */
void __board_i2c_post_init(int port)
{
}

void board_i2c_post_init(int port)
		__attribute__((weak, alias("__board_i2c_post_init")));

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

	board_i2c_post_init(I2C2);

	CPUTS("done\n");
	return EC_SUCCESS;
}

static int i2c_init1(void)
{
	/* enable clock */
	STM32_RCC_APB1ENR |= 1 << 21;

	/* force reset if the bus is stuck in BUSY state */
	if (STM32_I2C_SR2(I2C1) & 0x2) {
		STM32_I2C_CR1(I2C1) = 0x8000;
		STM32_I2C_CR1(I2C1) = 0x0000;
	}

	/* set clock configuration : standard mode (100kHz) */
	STM32_I2C_CCR(I2C1) = I2C_CCR;

	/* configuration : I2C mode / Periphal enabled, ACK enabled */
	STM32_I2C_CR1(I2C1) = (1 << 10) | (1 << 0);
	/* error and event interrupts enabled / input clock is 16Mhz */
	STM32_I2C_CR2(I2C1) = (1 << 9) | (1 << 8) | 0x10;

	/* clear status */
	STM32_I2C_SR1(I2C1) = 0;

	/* enable event and error interrupts */
	task_enable_irq(STM32_IRQ_I2C1_EV);
	task_enable_irq(STM32_IRQ_I2C1_ER);

	board_i2c_post_init(I2C1);

	return EC_SUCCESS;

}

static int i2c_init(void)
{
	int rc = 0;

	/* FIXME: Add #defines to determine which channels to init */
	rc |= i2c_init2();
	rc |= i2c_init1();

	return rc;
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);


/* Returns current command status (busy or not) */
static int host_command_get_comms_status(struct host_cmd_handler_args *args)
{
	struct ec_response_get_comms_status *r = args->response;

	r->flags = command_pending ? EC_COMMS_STATUS_PROCESSING : 0;
	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_GET_COMMS_STATUS,
		     host_command_get_comms_status,
		     EC_VER_MASK(0));

/* Resend the last saved response */
static int host_command_resend_response(struct host_cmd_handler_args *args)
{
	/* Handle resending response */
	args->result = saved_result;
	args->response_size = 0;

	saved_result = EC_RES_UNAVAILABLE;

	return EC_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_RESEND_RESPONSE,
		     host_command_resend_response,
		     EC_VER_MASK(0));


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
#ifdef CONFIG_DEBUG_I2C
	CPRINTF("CR1  : %016b\n", STM32_I2C_CR1(port));
	CPRINTF("CR2  : %016b\n", STM32_I2C_CR2(port));
	CPRINTF("SR2  : %016b\n", STM32_I2C_SR2(port));
	CPRINTF("SR1  : %016b\n", STM32_I2C_SR1(port));
	CPRINTF("OAR1 : %016b\n", STM32_I2C_OAR1(port));
	CPRINTF("OAR2 : %016b\n", STM32_I2C_OAR2(port));
	CPRINTF("DR   : %016b\n", STM32_I2C_DR(port));
	CPRINTF("CCR  : %016b\n", STM32_I2C_CCR(port));
	CPRINTF("TRISE: %016b\n", STM32_I2C_TRISE(port));
#endif /* CONFIG_DEBUG_I2C */
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
		if (t2.val - t1.val > I2C_TX_TIMEOUT_MASTER) {
			return EC_ERROR_TIMEOUT | (wait << 8);
		} else if (t2.val - t1.val > 150) {
			usleep(100);
		}
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

	/* we have not used the bus, just exit */
	if (rv == EC_ERROR_BUSY)
		return;

	/* EC_ERROR_TIMEOUT may have a code specifying where the timeout was */
	if ((rv & 0xff) == EC_ERROR_TIMEOUT) {
#ifdef CONFIG_DEBUG_I2C
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
	while (r & 2) {
		t2 = get_time();
		if (t2.val - t1.val > I2C_TX_TIMEOUT_MASTER) {
			dump_i2c_reg(port);
			goto cr_cleanup;
		}
		/* Send stop */
		master_stop(port);
		usleep(1000);
		r = STM32_I2C_SR2(port);
	}
cr_cleanup:
	/**
	 * reset control register to the default state :
	 * I2C mode / Periphal enabled, ACK enabled
	 */
	STM32_I2C_CR1(port) = (1 << 10) | (1 << 0);
}

static int i2c_master_transmit(int port, int slave_addr, uint8_t *data,
	int size, int stop)
{
	int rv, rv_start;
	struct dma_channel *chan;

	disable_ack(port);

	/* Configuring DMA1 channel DMAC_I2X_TX */
	chan = dma_get_channel(DMAC_I2C_TX);
	dma_prepare_tx(chan, size, (void *)&STM32_I2C_DR(port), data);
	dma_enable_tc_interrupt(DMAC_I2C_TX);

	/* Start the DMA */
	dma_go(chan);

	/* Configuring i2c2 to use DMA */
	STM32_I2C_CR2(port) |= CR2_DMAEN;

	/* Initialise i2c communication by sending START and ADDR */
	rv_start = master_start(port, slave_addr);

	/* If it started, wait for the transmission complete Interrupt */
	if (!rv_start)
		rv = task_wait_event(DMA_TRANSFER_TIMEOUT_US);

	dma_disable(DMAC_I2C_TX);
	dma_disable_tc_interrupt(DMAC_I2C_TX);
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
		dma_start_rx(DMAC_I2C_RX, size, (void *)&STM32_I2C_DR(port),
			data);

		dma_enable_tc_interrupt(DMAC_I2C_RX);

		STM32_I2C_CR2(port) |= CR2_DMAEN;
		STM32_I2C_CR2(port) |= CR2_LAST;

		rv_start = master_start(port, slave_addr | 1);
		if (!rv_start)
			rv = task_wait_event(DMA_TRANSFER_TIMEOUT_US);

		dma_disable(DMAC_I2C_RX);
		dma_disable_tc_interrupt(DMAC_I2C_RX);
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

/**
 * Perform an I2C transaction, involve a write, and optional read.
 *
 * @param port		I2C port to use (e.g. I2C_PORT_HOST)
 * @param slave_addr	Slave address of chip to access on I2C bus
 * @param out		Buffer containing bytes to output
 * @param out_bytes	Number of bytes to send (must be >0)
 * @param in		Buffer to place input bytes
 * @param in_bytes	Number of bytse to receive
 * @return 0 if ok, else ER_ERROR...
 */
static int i2c_xfer(int port, int slave_addr, uint8_t *out, int out_bytes,
	     uint8_t *in, int in_bytes)
{
	int rv;

	ASSERT(out && out_bytes);
	ASSERT(in || !in_bytes);

	disable_sleep(SLEEP_MASK_I2C);
	mutex_lock(&i2c_mutex);

	if (board_i2c_claim(port)) {
		rv = EC_ERROR_BUSY;
		goto err_claim;
	}

	disable_i2c_interrupt(port);

	rv = i2c_master_transmit(port, slave_addr, out, out_bytes,
				 in_bytes ? 0 : 1);
	if (!rv && in_bytes)
		rv = i2c_master_receive(port, slave_addr, in, in_bytes);
	handle_i2c_error(port, rv);

	enable_i2c_interrupt(port);

	board_i2c_release(port);

err_claim:
	mutex_unlock(&i2c_mutex);
	enable_sleep(SLEEP_MASK_I2C);

	return rv;
}

int i2c_read16(int port, int slave_addr, int offset, int *data)
{
	uint8_t reg, buf[2];
	int rv;

	reg = offset & 0xff;
	rv = i2c_xfer(port, slave_addr, &reg, 1, buf, 2);

	*data = (buf[1] << 8) | buf[0];

	return rv;
}

int i2c_write16(int port, int slave_addr, int offset, int data)
{
	uint8_t buf[3];

	buf[0] = offset & 0xff;
	buf[1] = data & 0xff;
	buf[2] = (data >> 8) & 0xff;

	return i2c_xfer(port, slave_addr, buf, sizeof(buf), NULL, 0);
}

int i2c_read8(int port, int slave_addr, int offset, int *data)
{
	uint8_t reg, buf[1];
	int rv;

	reg = offset & 0xff;
	rv = i2c_xfer(port, slave_addr, &reg, 1, buf, 1);

	*data = buf[0];

	return rv;
}

int i2c_write8(int port, int slave_addr, int offset, int data)
{
	uint8_t buf[2];

	buf[0] = offset & 0xff;
	buf[1] = data & 0xff;

	return i2c_xfer(port, slave_addr, buf, sizeof(buf), NULL, 0);
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
	int len)
{
	/* TODO: implement i2c_read_block and i2c_read_string */

	if (len && data)
		*data = 0;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

#ifdef I2C_PORT_HOST

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
DECLARE_CONSOLE_COMMAND(i2c, command_i2c,
			"r/r16/w/w16 slave_addr offset [value]",
			"Read write i2c",
			NULL);

#endif /* I2C_PORT_HOST */
