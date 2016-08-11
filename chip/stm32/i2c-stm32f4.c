/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

#define I2C_ERROR_FAILED_START EC_ERROR_INTERNAL_FIRST

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT_MASTER   (10 * MSEC)

/* Define I2C blocks available in stm32f4:
 * We have standard ST I2C blocks and a "fast mode plus" I2C block,
 * which do not share the same registers or functionality. So we'll need
 * two sets of functions to handle this for stm32f4. In stm32f446, we
 * only have one FMP block so we'll hardcode its port number.
 */
#define STM32F4_FMPI2C_PORT	3


static const struct dma_option dma_tx_option[I2C_PORT_COUNT] = {
	{STM32_DMAC_I2C1_TX, (void *)&STM32_I2C_DR(STM32_I2C1_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_I2C1_TX_REQ_CH)},
	{STM32_DMAC_I2C2_TX, (void *)&STM32_I2C_DR(STM32_I2C2_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_I2C2_TX_REQ_CH)},
	{STM32_DMAC_I2C3_TX, (void *)&STM32_I2C_DR(STM32_I2C3_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_I2C3_TX_REQ_CH)},
	{STM32_DMAC_FMPI2C4_TX, (void *)&STM32_FMPI2C_TXDR(STM32_FMPI2C4_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_FMPI2C4_TX_REQ_CH)},
};

static const struct dma_option dma_rx_option[I2C_PORT_COUNT] = {
	{STM32_DMAC_I2C1_RX, (void *)&STM32_I2C_DR(STM32_I2C1_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_I2C1_RX_REQ_CH)},
	{STM32_DMAC_I2C2_RX, (void *)&STM32_I2C_DR(STM32_I2C2_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_I2C2_RX_REQ_CH)},
	{STM32_DMAC_I2C3_RX, (void *)&STM32_I2C_DR(STM32_I2C3_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_I2C3_RX_REQ_CH)},
	{STM32_DMAC_FMPI2C4_RX, (void *)&STM32_FMPI2C_RXDR(STM32_FMPI2C4_PORT),
	 STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	 STM32_DMA_CCR_CHANNEL(STM32_FMPI2C4_RX_REQ_CH)},
};

/* Callabck for ISR to wake task on DMA complete. */
static inline void _i2c_dma_wake_callback(void *cb_data, int port)
{
	task_id_t id = (task_id_t)(int)cb_data;

	if (id != TASK_ID_INVALID)
		task_set_event(id, TASK_EVENT_I2C_COMPLETION(port), 0);
}

/* Each callback is hardcoded to an I2C channel. */
static void _i2c_dma_wake_callback_0(void *cb_data)
{
	_i2c_dma_wake_callback(cb_data, 0);
}

static void _i2c_dma_wake_callback_1(void *cb_data)
{
	_i2c_dma_wake_callback(cb_data, 1);
}

static void _i2c_dma_wake_callback_2(void *cb_data)
{
	_i2c_dma_wake_callback(cb_data, 2);
}

static void _i2c_dma_wake_callback_3(void *cb_data)
{
	_i2c_dma_wake_callback(cb_data, 3);
}

/* void (*callback)(void *) */
static void (*i2c_callbacks[I2C_PORT_COUNT])(void *) = {
	_i2c_dma_wake_callback_0,
	_i2c_dma_wake_callback_1,
	_i2c_dma_wake_callback_2,
	_i2c_dma_wake_callback_3,
};

/* Enable the I2C interrupt callback for this port. */
void i2c_dma_enable_tc_interrupt(enum dma_channel stream, int port)
{
	dma_enable_tc_interrupt_callback(stream, i2c_callbacks[port],
					 (void *)(int)task_get_current());
}

/**
 * Wait for SR1 register to contain the specified mask of 0 or 1.
 *
 * @param port		I2C port
 * @param mask		mask of bits of interest
 * @param val		desired value of bits of interest
 * @param poll		uS poll frequency
 *
 * @return		EC_SUCCESS, EC_ERROR_TIMEOUT if timed out waiting, or
 * EC_ERROR_UNKNOWN if an error bit appeared in the status register.
 */
#define SET 0xffffffff
#define UNSET 0
static int wait_sr1_poll(int port, int mask, int val, int poll)
{
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_MASTER;

	while (get_time().val < timeout) {
		int sr1 = STM32_I2C_SR1(port);

		/* Check for errors */
		if (sr1 & (STM32_I2C_SR1_ARLO | STM32_I2C_SR1_BERR |
			   STM32_I2C_SR1_AF)) {
			return EC_ERROR_UNKNOWN;
		}

		/* Check for desired mask */
		if ((sr1 & mask) == (val & mask))
			return EC_SUCCESS;

		/* I2C is slow, so let other things run while we wait */
		usleep(poll);
	}

	CPRINTS("I2C timeout: p:%d m:%x", port, mask);
	return EC_ERROR_TIMEOUT;
}

/* Wait for SR1 register to contain the specified mask of ones */
static int wait_sr1(int port, int mask)
{
	return wait_sr1_poll(port, mask, SET, 100);
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
	rv = wait_sr1_poll(port, STM32_I2C_SR1_SB, SET, 1);
	if (rv)
		return I2C_ERROR_FAILED_START;

	/* Write slave address */
	STM32_I2C_DR(port) = slave_addr & 0xff;
	rv = wait_sr1_poll(port, STM32_I2C_SR1_ADDR, SET, 1);
	if (rv)
		return rv;

	/* Read SR2 to clear ADDR bit */
	rv = STM32_I2C_SR2(port);

	return EC_SUCCESS;
}

/**
 * Find the i2c port structure associated with the port.
 *
 * @return i2c_port_t * associated with this port number.
 */
static const struct i2c_port_t *find_port(int port)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++) {
		if (p->port == port)
			return p;
	}
	CPRINTS("I2C port %d invalid! Crashing now.", port);
	return NULL;
}

/**
 * Wait for ISR register to contain the specified mask.
 *
 * @param port		I2C port
 * @param mask		mask of bits of interest
 * @param val		desired value of bits of interest
 * @param poll		uS poll frequency
 *
 * @return		EC_SUCCESS, EC_ERROR_TIMEOUT if timed out waiting, or
 * EC_ERROR_UNKNOWN if an error bit appeared in the status register.
 */
static int wait_fmpi2c_isr_poll(int port, int mask, int val, int poll)
{
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_MASTER;

	while (get_time().val < timeout) {
		int isr = STM32_FMPI2C_ISR(port);

		/* Check for errors */
		if (isr & (FMPI2C_ISR_ARLO | FMPI2C_ISR_BERR |
			   FMPI2C_ISR_NACKF)) {
			return EC_ERROR_UNKNOWN;
		}

		/* Check for desired mask */
		if ((isr & mask) == (val & mask))
			return EC_SUCCESS;

		/* I2C is slow, so let other things run while we wait */
		usleep(poll);
	}

	CPRINTS("FMPI2C timeout p:%d, m:0x%08x", port, mask);
	return EC_ERROR_TIMEOUT;
}

/* Wait for ISR register to contain the specified mask of ones */
static int wait_fmpi2c_isr(int port, int mask)
{
	return wait_fmpi2c_isr_poll(port, mask, SET, 100);
}

/**
 * Send a start condition and slave address on the specified port.
 *
 * @param port		I2C port
 * @param slave_addr	Slave address
 * @param size		bytes to transfer
 * @param is_read	read, or write?
 *
 * @return Non-zero if error.
 */
static int send_fmpi2c_start(int port, int slave_addr, int size, int is_read)
{
	uint32_t reg;

	/* Send start bit */
	reg = STM32_FMPI2C_CR2(port);
	reg &= ~(FMPI2C_CR2_SADD_MASK | FMPI2C_CR2_SIZE_MASK |
		FMPI2C_CR2_RELOAD | FMPI2C_CR2_AUTOEND |
		FMPI2C_CR2_RD_WRN | FMPI2C_CR2_START | FMPI2C_CR2_STOP);
	reg |= FMPI2C_CR2_START | FMPI2C_CR2_AUTOEND |
		FMPI2C_CR2_SADD(slave_addr) | FMPI2C_CR2_SIZE(size) |
		(is_read ? FMPI2C_CR2_RD_WRN : 0);
	STM32_FMPI2C_CR2(port) = reg;

	return EC_SUCCESS;
}

/**
 * Set i2c clock rate..
 *
 * @param p		I2C port struct
 */
static void i2c_set_freq_port(const struct i2c_port_t *p)
{
	int port = p->port;
	int freq = clock_get_freq();

	if (p->port == STM32F4_FMPI2C_PORT) {
		int prescalar;
		int actual;
		uint32_t reg;

		/* FMP I2C clock set. */
		STM32_FMPI2C_CR1(port) &= ~FMPI2C_CR1_PE;
		prescalar = (freq / (p->kbps * 1000 *
			     (0x12 + 1 + 0xe + 1 + 1))) - 1;
		actual = freq / ((prescalar + 1) * (0x12 + 1 + 0xe + 1 + 1));

		reg = FMPI2C_TIMINGR_SCLL(0x12) |
			FMPI2C_TIMINGR_SCLH(0xe) |
			FMPI2C_TIMINGR_PRESC(prescalar);
		STM32_FMPI2C_TIMINGR(port) = reg;

		CPRINTS("port %d target %d, pre %d, act %d, reg 0x%08x",
			port, p->kbps, prescalar, actual, reg);

		STM32_FMPI2C_CR1(port) |= FMPI2C_CR1_PE;
		udelay(10);
	} else {
		/* Force peripheral reset and disable port */
		STM32_I2C_CR1(port) = STM32_I2C_CR1_SWRST;
		STM32_I2C_CR1(port) = 0;

		/* Set clock frequency */
		if (p->kbps > 100) {
			STM32_I2C_CCR(port) = freq / (2 * MSEC * p->kbps);
		} else {
			STM32_I2C_CCR(port) = STM32_I2C_CCR_FM
				| STM32_I2C_CCR_DUTY
				| (freq / (16 + 9 * MSEC * p->kbps));
		}
		STM32_I2C_CR2(port) = freq / SECOND;
		STM32_I2C_TRISE(port) = freq / SECOND + 1;

		/* Enable port */
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_PE;
	}
}

/**
 * Initialize on the specified I2C port.
 *
 * @param p		the I2c port
 */
static void i2c_init_port(const struct i2c_port_t *p)
{
	int port = p->port;

	/* Configure GPIOs, clocks */
	gpio_config_module(MODULE_I2C, 1);
	clock_enable_module(MODULE_I2C, 1);

	if (p->port == STM32F4_FMPI2C_PORT) {
		/* FMP I2C block */
		/* Set timing (?) */
		STM32_FMPI2C_TIMINGR(port) = TIMINGR_THE_RIGHT_VALUE;
		udelay(10);
		/* Device enable */
		STM32_FMPI2C_CR1(port) |= FMPI2C_CR1_PE;
		/* Need to wait 3 APB cycles */
		udelay(10);
		/* Device only. */
		STM32_FMPI2C_OAR1(port) = 0;
		STM32_FMPI2C_CR2(port) |= FMPI2C_CR2_AUTOEND;
	} else {
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_SWRST;
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_SWRST;
		udelay(10);
	}

	/* Set up initial bus frequencies */
	i2c_set_freq_port(p);
}

/*****************************************************************************/
/* Interface */

/**
 * Clear status regs on the specified I2C port.
 *
 * @param port		the I2c port
 */
static void fmpi2c_clear_regs(int port)
{
	/* Clear status */
	STM32_FMPI2C_ICR(port) = 0xffffffff;

	/* Clear start, stop, NACK, etc. bits to get us in a known state */
	STM32_FMPI2C_CR2(port) &= ~(FMPI2C_CR2_START | FMPI2C_CR2_STOP |
				FMPI2C_CR2_RD_WRN | FMPI2C_CR2_NACK |
				FMPI2C_CR2_AUTOEND |
				FMPI2C_CR2_SADD_MASK | FMPI2C_CR2_SIZE_MASK);
}

/**
 * Perform an i2c transaction
 *
 * @param port		i2c port to use
 * @param slave_addr	the i2c slave addr
 * @param out		source buffer for data
 * @param out_bytes	bytes of data to write
 * @param in		destination buffer for data
 * @param in_bytes	bytes of data to read
 * @param flags		user cached I2C state
 *
 * @return		EC_SUCCESS on success.
 */
static int chip_fmpi2c_xfer(int port, int slave_addr, const uint8_t *out,
		     int out_bytes, uint8_t *in, int in_bytes, int flags)
{
	int started = (flags & I2C_XFER_START) ? 0 : 1;
	int rv = EC_SUCCESS;
	int i;

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);
	ASSERT(!started);

	if (STM32_FMPI2C_ISR(port) & FMPI2C_ISR_BUSY) {
		CPRINTS("fmpi2c port %d busy", port);
		return EC_ERROR_BUSY;
	}

	fmpi2c_clear_regs(port);

	/* No out bytes and no in bytes means just check for active */
	if (out_bytes || !in_bytes) {
		rv = send_fmpi2c_start(
			port, slave_addr, out_bytes, FMPI2C_WRITE);
		if (rv)
			goto xfer_exit;

		/* Write data, if any */
		for (i = 0; i < out_bytes; i++) {
			rv = wait_fmpi2c_isr(port, FMPI2C_ISR_TXIS);
			if (rv)
				goto xfer_exit;

			/* Write next data byte */
			STM32_FMPI2C_TXDR(port) = out[i];
		}

		/* Wait for transaction STOP. */
		wait_fmpi2c_isr(port, FMPI2C_ISR_STOPF);
	}

	if (in_bytes) {
		int rv_start;
		const struct dma_option *dma = dma_rx_option + port;

		dma_start_rx(dma, in_bytes, in);
		i2c_dma_enable_tc_interrupt(dma->channel, port);

		rv_start = send_fmpi2c_start(
				port, slave_addr, in_bytes, FMPI2C_READ);
		if (rv_start)
			goto xfer_exit;

		rv = wait_fmpi2c_isr(port, FMPI2C_ISR_RXNE);
		if (rv)
			goto xfer_exit;
		STM32_FMPI2C_CR1(port) |= FMPI2C_CR1_RXDMAEN;

		if (!rv_start) {
			rv = task_wait_event_mask(
					TASK_EVENT_I2C_COMPLETION(port),
					DMA_TRANSFER_TIMEOUT_US);
			if (rv & TASK_EVENT_I2C_COMPLETION(port))
				rv = EC_SUCCESS;
			else
				rv = EC_ERROR_TIMEOUT;
		}

		dma_disable(dma->channel);
		dma_disable_tc_interrupt(dma->channel);

		/* Validate i2c is STOPped */
		if (!rv)
			rv = wait_fmpi2c_isr(port, FMPI2C_ISR_STOPF);

		STM32_FMPI2C_CR1(port) &= ~FMPI2C_CR1_RXDMAEN;

		if (rv_start)
			rv = rv_start;
	}

 xfer_exit:
	/* On error, queue a stop condition */
	if (rv) {
		flags |= I2C_XFER_STOP;
		STM32_FMPI2C_CR2(port) |= FMPI2C_CR2_STOP;

		/*
		 * If failed at sending start, try resetting the port
		 * to unwedge the bus.
		 */
		if (rv == I2C_ERROR_FAILED_START) {
			const struct i2c_port_t *p;

			CPRINTS("chip_fmpi2c_xfer start error; "
				"unwedging and resetting i2c %d", port);

			p = find_port(port);
			i2c_unwedge(port);
			i2c_init_port(p);
		}
	}

	/* If a stop condition is queued, wait for it to take effect */
	if (flags & I2C_XFER_STOP) {
		/* Wait up to 100 us for bus idle */
		for (i = 0; i < 10; i++) {
			if (!(STM32_FMPI2C_ISR(port) & FMPI2C_ISR_BUSY))
				break;
			usleep(10);
		}

		/*
		 * Allow bus to idle for at least one 100KHz clock = 10 us.
		 * This allows slaves on the bus to detect bus-idle before
		 * the next start condition.
		 */
		STM32_FMPI2C_CR1(port) &= ~FMPI2C_CR1_PE;
		usleep(10);
		STM32_FMPI2C_CR1(port) |= FMPI2C_CR1_PE;
	}

	return rv;
}


/**
 * Clear status regs on the specified I2C port.
 *
 * @param port		the I2c port
 */
static void i2c_clear_regs(int port)
{
	/*
	 * Clear status
	 *
	 * TODO(crosbug.com/p/29314): should check for any leftover error
	 * status, and reset the port if present.
	 */
	STM32_I2C_SR1(port) = 0;

	/* Clear start, stop, POS, ACK bits to get us in a known state */
	STM32_I2C_CR1(port) &= ~(STM32_I2C_CR1_START |
				 STM32_I2C_CR1_STOP |
				 STM32_I2C_CR1_POS |
				 STM32_I2C_CR1_ACK);
}

/*****************************************************************************
 * Exported functions declared in i2c.h
 */

/* Perform an i2c transaction. */
int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_bytes,
		  uint8_t *in, int in_bytes, int flags)
{
	int started = (flags & I2C_XFER_START) ? 0 : 1;
	int rv = EC_SUCCESS;
	int i;
	const struct i2c_port_t *p = find_port(port);

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);
	ASSERT(!started);

	if (p->port == STM32F4_FMPI2C_PORT) {
		return chip_fmpi2c_xfer(port, slave_addr, out, out_bytes,
			in, in_bytes, flags);
	}

	i2c_clear_regs(port);

	/* No out bytes and no in bytes means just check for active */
	if (out_bytes || !in_bytes) {
		rv = send_start(port, slave_addr);
		if (rv)
			goto xfer_exit;

		/* Write data, if any */
		for (i = 0; i < out_bytes; i++) {
			/* Write next data byte */
			STM32_I2C_DR(port) = out[i];

			rv = wait_sr1(port, STM32_I2C_SR1_BTF);
			if (rv)
				goto xfer_exit;
		}

		/* If no input bytes, queue stop condition */
		if (!in_bytes && (flags & I2C_XFER_STOP))
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;
	}

	if (in_bytes) {
		int rv_start;

		const struct dma_option *dma = dma_rx_option + port;

		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_POS;
		dma_start_rx(dma, in_bytes, in);
		i2c_dma_enable_tc_interrupt(dma->channel, port);

		/* Setup ACK/POS before sending start as per user manual */
		if (in_bytes == 2)
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_POS;
		else if (in_bytes != 1)
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_ACK;

		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_STOP;

		STM32_I2C_CR2(port) |= STM32_I2C_CR2_LAST;
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_DMAEN;

		rv_start = send_start(port, slave_addr | 0x01);

		if ((in_bytes == 1) && (flags & I2C_XFER_STOP))
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;

		if (!rv_start) {
			rv = task_wait_event_mask(
				TASK_EVENT_I2C_COMPLETION(port),
				DMA_TRANSFER_TIMEOUT_US);
			if (rv & TASK_EVENT_I2C_COMPLETION(port))
				rv = EC_SUCCESS;
			else
				rv = EC_ERROR_TIMEOUT;
		}

		dma_disable(dma->channel);
		dma_disable_tc_interrupt(dma->channel);
		STM32_I2C_CR2(port) &= ~STM32_I2C_CR2_DMAEN;
		/* Disable ack */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_ACK;

		if (rv_start)
			rv = rv_start;

		/* Send stop. */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_ACK;
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;
		STM32_I2C_CR2(port) &= ~STM32_I2C_CR2_LAST;
		STM32_I2C_CR2(port) &= ~STM32_I2C_CR2_DMAEN;
	}

 xfer_exit:
	/* On error, queue a stop condition */
	if (rv) {
		flags |= I2C_XFER_STOP;
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_STOP;

		/*
		 * If failed at sending start, try resetting the port
		 * to unwedge the bus.
		 */
		if (rv == I2C_ERROR_FAILED_START) {
			const struct i2c_port_t *p;

			CPRINTS("chip_i2c_xfer start error; "
				"unwedging and resetting i2c %d", port);

			p = find_port(port);
			i2c_unwedge(port);
			i2c_init_port(p);
		}
	}

	/* If a stop condition is queued, wait for it to take effect */
	if (flags & I2C_XFER_STOP) {
		/* Wait up to 100 us for bus idle */
		for (i = 0; i < 10; i++) {
			if (!(STM32_I2C_SR2(port) & STM32_I2C_SR2_BUSY))
				break;
			usleep(10);
		}

		/*
		 * Allow bus to idle for at least one 100KHz clock = 10 us.
		 * This allows slaves on the bus to detect bus-idle before
		 * the next start condition.
		 */
		usleep(10);
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

	/* If no SDA pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		(i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

/*****************************************************************************/
/* Hooks */

/* Handle CPU clock changing frequency */
static void i2c_freq_change(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_set_freq_port(p);
}

/* Handle an upcoming frequency change. */
static void i2c_pre_freq_change_hook(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	/* Lock I2C ports so freq change can't interrupt an I2C transaction */
	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_lock(p->port, 1);
}
DECLARE_HOOK(HOOK_PRE_FREQ_CHANGE, i2c_pre_freq_change_hook, HOOK_PRIO_DEFAULT);

/* Handle a frequency change */
static void i2c_freq_change_hook(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	i2c_freq_change();

	/* Unlock I2C ports we locked in pre-freq change hook */
	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_lock(p->port, 0);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_change_hook, HOOK_PRIO_DEFAULT);

/* Init all available i2c ports */
static void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_init_port(p);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);
