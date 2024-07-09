/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

#define I2C_ERROR_FAILED_START EC_ERROR_INTERNAL_FIRST

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT_CONTROLLER (10 * MSEC)

#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS
#if (I2C_PORT_EC == STM32_I2C1_PORT)
#define IRQ_PERIPHERAL_EV STM32_IRQ_I2C1_EV
#define IRQ_PERIPHERAL_ER STM32_IRQ_I2C1_ER
#else
#define IRQ_PERIPHERAL_EV STM32_IRQ_I2C2_EV
#define IRQ_PERIPHERAL_ER STM32_IRQ_I2C2_ER
#endif
#endif

/* Define I2C blocks available in stm32f4:
 * We have standard ST I2C blocks and a "fast mode plus" I2C block,
 * which do not share the same registers or functionality. So we'll need
 * two sets of functions to handle this for stm32f4. In stm32f446, we
 * only have one FMP block so we'll hardcode its port number.
 */
#define STM32F4_FMPI2C_PORT 3

static const __unused struct dma_option dma_tx_option[I2C_PORT_COUNT] = {
	{ STM32_DMAC_I2C1_TX, (void *)&STM32_I2C_DR(STM32_I2C1_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_I2C1_TX_REQ_CH) },
	{ STM32_DMAC_I2C2_TX, (void *)&STM32_I2C_DR(STM32_I2C2_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_I2C2_TX_REQ_CH) },
	{ STM32_DMAC_I2C3_TX, (void *)&STM32_I2C_DR(STM32_I2C3_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_I2C3_TX_REQ_CH) },
	{ STM32_DMAC_FMPI2C4_TX, (void *)&STM32_FMPI2C_TXDR(STM32_FMPI2C4_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_FMPI2C4_TX_REQ_CH) },
};

static const struct dma_option dma_rx_option[I2C_PORT_COUNT] = {
	{ STM32_DMAC_I2C1_RX, (void *)&STM32_I2C_DR(STM32_I2C1_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_I2C1_RX_REQ_CH) },
	{ STM32_DMAC_I2C2_RX, (void *)&STM32_I2C_DR(STM32_I2C2_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_I2C2_RX_REQ_CH) },
	{ STM32_DMAC_I2C3_RX, (void *)&STM32_I2C_DR(STM32_I2C3_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_I2C3_RX_REQ_CH) },
	{ STM32_DMAC_FMPI2C4_RX, (void *)&STM32_FMPI2C_RXDR(STM32_FMPI2C4_PORT),
	  STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
		  STM32_DMA_CCR_CHANNEL(STM32_FMPI2C4_RX_REQ_CH) },
};

/* Callback for ISR to wake task on DMA complete. */
static inline void _i2c_dma_wake_callback(void *cb_data, int port)
{
	task_id_t id = (task_id_t)(int)cb_data;

	if (id != TASK_ID_INVALID)
		task_set_event(id, TASK_EVENT_I2C_COMPLETION(port));
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
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_CONTROLLER;

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
		crec_usleep(poll);
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
 * Send a start condition and peripheral address on the specified port.
 *
 * @param port		I2C port
 * @param addr_8bit	I2C address, with LSB set for receive-mode
 *
 * @return Non-zero if error.
 */
static int send_start(const int port, const uint16_t addr_8bit)
{
	int rv;

	/* Send start bit */
	STM32_I2C_CR1(port) |= STM32_I2C_CR1_START;
	rv = wait_sr1_poll(port, STM32_I2C_SR1_SB, SET, 1);
	if (rv)
		return I2C_ERROR_FAILED_START;

	/* Write peripheral address */
	STM32_I2C_DR(port) = addr_8bit;
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
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_CONTROLLER;

	while (get_time().val < timeout) {
		int isr = STM32_FMPI2C_ISR(port);

		/* Check for errors */
		if (isr &
		    (FMPI2C_ISR_ARLO | FMPI2C_ISR_BERR | FMPI2C_ISR_NACKF)) {
			return EC_ERROR_UNKNOWN;
		}

		/* Check for desired mask */
		if ((isr & mask) == (val & mask))
			return EC_SUCCESS;

		/* I2C is slow, so let other things run while we wait */
		crec_usleep(poll);
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
 * Send a start condition and peripheral address on the specified port.
 *
 * @param port		I2C port
 * @param addr_8bit	I2C address
 * @param size		bytes to transfer
 * @param is_read	read, or write?
 *
 * @return Non-zero if error.
 */
static int send_fmpi2c_start(const int port, const uint16_t addr_8bit, int size,
			     int is_read)
{
	uint32_t reg;

	/* Send start bit */
	reg = STM32_FMPI2C_CR2(port);
	reg &= ~(FMPI2C_CR2_SADD_MASK | FMPI2C_CR2_SIZE_MASK |
		 FMPI2C_CR2_RELOAD | FMPI2C_CR2_AUTOEND | FMPI2C_CR2_RD_WRN |
		 FMPI2C_CR2_START | FMPI2C_CR2_STOP);
	reg |= FMPI2C_CR2_START | FMPI2C_CR2_AUTOEND | addr_8bit |
	       FMPI2C_CR2_SIZE(size) | (is_read ? FMPI2C_CR2_RD_WRN : 0);
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
		prescalar =
			(freq / (p->kbps * 1000 * (0x12 + 1 + 0xe + 1 + 1))) -
			1;
		actual = freq / ((prescalar + 1) * (0x12 + 1 + 0xe + 1 + 1));

		reg = FMPI2C_TIMINGR_SCLL(0x12) | FMPI2C_TIMINGR_SCLH(0xe) |
		      FMPI2C_TIMINGR_PRESC(prescalar);
		STM32_FMPI2C_TIMINGR(port) = reg;

		CPRINTS("port %d target %d, pre %d, act %d, reg 0x%08x", port,
			p->kbps, prescalar, actual, reg);

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
			STM32_I2C_CCR(port) =
				STM32_I2C_CCR_FM | STM32_I2C_CCR_DUTY |
				(freq / (16 + 9 * MSEC * p->kbps));
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
	STM32_FMPI2C_CR2(port) &=
		~(FMPI2C_CR2_START | FMPI2C_CR2_STOP | FMPI2C_CR2_RD_WRN |
		  FMPI2C_CR2_NACK | FMPI2C_CR2_AUTOEND | FMPI2C_CR2_SADD_MASK |
		  FMPI2C_CR2_SIZE_MASK);
}

/**
 * Perform an i2c transaction
 *
 * @param port		i2c port to use
 * @param addr_8bit	the i2c address
 * @param out		source buffer for data
 * @param out_bytes	bytes of data to write
 * @param in		destination buffer for data
 * @param in_bytes	bytes of data to read
 * @param flags		user cached I2C state
 *
 * @return		EC_SUCCESS on success.
 */
static int chip_fmpi2c_xfer(const int port, const uint16_t addr_8bit,
			    const uint8_t *out, int out_bytes, uint8_t *in,
			    int in_bytes, int flags)
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
		rv = send_fmpi2c_start(port, addr_8bit, out_bytes,
				       FMPI2C_WRITE);
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

		rv_start = send_fmpi2c_start(port, addr_8bit, in_bytes,
					     FMPI2C_READ);
		if (rv_start)
			goto xfer_exit;

		rv = wait_fmpi2c_isr(port, FMPI2C_ISR_RXNE);
		if (rv)
			goto xfer_exit;
		STM32_FMPI2C_CR1(port) |= FMPI2C_CR1_RXDMAEN;

		rv = task_wait_event_mask(TASK_EVENT_I2C_COMPLETION(port),
					  DMA_TRANSFER_TIMEOUT_US);
		if (rv & TASK_EVENT_I2C_COMPLETION(port))
			rv = EC_SUCCESS;
		else
			rv = EC_ERROR_TIMEOUT;

		dma_disable(dma->channel);
		dma_disable_tc_interrupt(dma->channel);

		/* Validate i2c is STOPped */
		if (!rv)
			rv = wait_fmpi2c_isr(port, FMPI2C_ISR_STOPF);

		STM32_FMPI2C_CR1(port) &= ~FMPI2C_CR1_RXDMAEN;
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
				"unwedging and resetting i2c %d",
				port);

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
			crec_usleep(10);
		}

		/*
		 * Allow bus to idle for at least one 100KHz clock = 10 us.
		 * This allows peripherals on the bus to detect bus-idle before
		 * the next start condition.
		 */
		STM32_FMPI2C_CR1(port) &= ~FMPI2C_CR1_PE;
		crec_usleep(10);
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
	STM32_I2C_CR1(port) &= ~(STM32_I2C_CR1_START | STM32_I2C_CR1_STOP |
				 STM32_I2C_CR1_POS | STM32_I2C_CR1_ACK);
}

/*****************************************************************************
 * Exported functions declared in i2c.h
 */

/* Perform an i2c transaction. */
int chip_i2c_xfer(const int port, const uint16_t addr_flags, const uint8_t *out,
		  int out_bytes, uint8_t *in, int in_bytes, int flags)
{
	int addr_8bit = I2C_STRIP_FLAGS(addr_flags) << 1;
	int started = (flags & I2C_XFER_START) ? 0 : 1;
	int rv = EC_SUCCESS;
	int i;
	const struct i2c_port_t *p = find_port(port);

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);
	ASSERT(!started);

	if (p->port == STM32F4_FMPI2C_PORT) {
		return chip_fmpi2c_xfer(port, addr_8bit, out, out_bytes, in,
					in_bytes, flags);
	}

	i2c_clear_regs(port);

	/* No out bytes and no in bytes means just check for active */
	if (out_bytes || !in_bytes) {
		rv = send_start(port, addr_8bit);
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

		rv_start = send_start(port, addr_8bit | 0x01);

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
				"unwedging and resetting i2c %d",
				port);

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
			crec_usleep(10);
		}

		/*
		 * Allow bus to idle for at least one 100KHz clock = 10 us.
		 * This allows peripherals on the bus to detect bus-idle before
		 * the next start condition.
		 */
		crec_usleep(10);
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

#ifdef CONFIG_I2C_CONTROLLER
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
#endif

/*****************************************************************************/
/* Peripheral */
#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS
/* Host command peripheral */
/*
 * Buffer for received host command packets (including prefix byte on request,
 * and result/size on response).  After any protocol-specific headers, the
 * buffers must be 32-bit aligned.
 */
static uint8_t host_buffer_padded[I2C_MAX_HOST_PACKET_SIZE + 4 +
				  CONFIG_I2C_EXTRA_PACKET_SIZE] __aligned(4);
static uint8_t *const host_buffer = host_buffer_padded + 2;
static uint8_t params_copy[I2C_MAX_HOST_PACKET_SIZE] __aligned(4);
static int host_i2c_resp_port;
static int tx_pending;
static int tx_index, tx_end;
static struct host_packet i2c_packet;

static void i2c_send_response_packet(struct host_packet *pkt)
{
	int size = pkt->response_size;
	uint8_t *out = host_buffer;

	/* Ignore host command in-progress */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result and size to first two bytes. */
	*out++ = pkt->driver_result;
	*out++ = size;

	/* host_buffer data range */
	tx_index = 0;
	tx_end = size + 2;

	/*
	 * Set the transmitter to be in 'not full' state to keep sending
	 * '0xec' in the event loop. Because of this, the controller i2c
	 * doesn't need to snoop the response stream to abort transaction.
	 */
	STM32_I2C_CR2(host_i2c_resp_port) |= STM32_I2C_CR2_ITBUFEN;
}

/* Process the command in the i2c host buffer */
static void i2c_process_command(void)
{
	char *buff = host_buffer;

	/*
	 * TODO(crosbug.com/p/29241): Combine this functionality with the
	 * i2c_process_command function in chip/stm32/i2c-stm32f.c to make one
	 * host command i2c process function which handles all protocol
	 * versions.
	 */
	i2c_packet.send_response = i2c_send_response_packet;

	i2c_packet.request = (const void *)(&buff[1]);
	i2c_packet.request_temp = params_copy;
	i2c_packet.request_max = sizeof(params_copy);
	/* Don't know the request size so pass in the entire buffer */
	i2c_packet.request_size = I2C_MAX_HOST_PACKET_SIZE;

	/*
	 * Stuff response at buff[2] to leave the first two bytes of
	 * buffer available for the result and size to send over i2c.  Note
	 * that this 2-byte offset and the 2-byte offset from host_buffer
	 * add up to make the response buffer 32-bit aligned.
	 */
	i2c_packet.response = (void *)(&buff[2]);
	i2c_packet.response_max = I2C_MAX_HOST_PACKET_SIZE;
	i2c_packet.response_size = 0;

	if (*buff >= EC_COMMAND_PROTOCOL_3) {
		i2c_packet.driver_result = EC_RES_SUCCESS;
	} else {
		/* Only host command protocol 3 is supported. */
		i2c_packet.driver_result = EC_RES_INVALID_HEADER;
	}
	host_packet_receive(&i2c_packet);
}

#ifdef CONFIG_BOARD_I2C_ADDR_FLAGS
static void i2c_send_board_response(int len)
{
	/* host_buffer data range, beyond this length, will return 0xec */
	tx_index = 0;
	tx_end = len;

	/* enable transmit interrupt and use irq to send data back */
	STM32_I2C_CR2(host_i2c_resp_port) |= STM32_I2C_CR2_ITBUFEN;
}

static void i2c_process_board_command(int read, int addr, int len)
{
	board_i2c_process(read, addr, len, &host_buffer[0],
			  i2c_send_board_response);
}
#endif

static void i2c_event_handler(int port)
{
	volatile uint32_t i2c_cr1;
	volatile uint32_t i2c_sr2;
	volatile uint32_t i2c_sr1;
	static int rx_pending, buf_idx;
	static uint16_t addr_8bit;

	volatile uint32_t unused __attribute__((unused));

	i2c_cr1 = STM32_I2C_CR1(port);
	i2c_sr2 = STM32_I2C_SR2(port);
	i2c_sr1 = STM32_I2C_SR1(port);

	/*
	 * Check for error conditions. Note, arbitration loss and bus error
	 * are the only two errors we can get as a peripheral allowing clock
	 * stretching and in non-SMBus mode.
	 */
	if (i2c_sr1 & (STM32_I2C_SR1_ARLO | STM32_I2C_SR1_BERR)) {
		rx_pending = 0;
		tx_pending = 0;
		/* Disable buffer interrupt */
		STM32_I2C_CR2(port) &= ~STM32_I2C_CR2_ITBUFEN;
		/* Clear error status bits */
		STM32_I2C_SR1(port) &=
			~(STM32_I2C_SR1_ARLO | STM32_I2C_SR1_BERR);
	}

	/* Transfer matched our peripheral address */
	if (i2c_sr1 & STM32_I2C_SR1_ADDR) {
		addr_8bit = ((i2c_sr2 & STM32_I2C_SR2_DUALF) ?
				     STM32_I2C_OAR2(port) :
				     STM32_I2C_OAR1(port)) &
			    0xfe;
		if (i2c_sr2 & STM32_I2C_SR2_TRA) {
			/* Transmitter peripheral */
			i2c_sr1 |= STM32_I2C_SR1_TXE;
#ifdef CONFIG_BOARD_I2C_ADDR_FLAGS
			if (!rx_pending && !tx_pending) {
				tx_pending = 1;
				i2c_process_board_command(1, addr_8bit, 0);
			}
#endif
		} else {
			/* Receiver peripheral */
			buf_idx = 0;
			rx_pending = 1;
		}

		/* Enable buffer interrupt to start receive/response */
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_ITBUFEN;
		/* Clear ADDR bit */
		unused = STM32_I2C_SR1(port);
		unused = STM32_I2C_SR2(port);
		/* Inhibit stop mode when addressed until STOPF flag is set */
		disable_sleep(SLEEP_MASK_I2C_PERIPHERAL);
	}

	/* I2C in peripheral transmitter */
	if (i2c_sr2 & STM32_I2C_SR2_TRA) {
		if (i2c_sr1 & (STM32_I2C_SR1_BTF | STM32_I2C_SR1_TXE)) {
			if (tx_pending) {
				if (tx_index < tx_end) {
					STM32_I2C_DR(port) =
						host_buffer[tx_index++];
				} else {
					STM32_I2C_DR(port) = 0xec;
					tx_index = 0;
					tx_end = 0;
					tx_pending = 0;
				}
			} else if (rx_pending) {
				host_i2c_resp_port = port;
				/* Disable buffer interrupt */
				STM32_I2C_CR2(port) &= ~STM32_I2C_CR2_ITBUFEN;
#ifdef CONFIG_BOARD_I2C_ADDR_FLAGS
				if ((addr_8bit >> 1) ==
				    I2C_STRIP_FLAGS(
					    CONFIG_BOARD_I2C_ADDR_FLAGS))
					i2c_process_board_command(1, addr_8bit,
								  buf_idx);
				else
#endif
					i2c_process_command();
				/* Reset host buffer */
				rx_pending = 0;
				tx_pending = 1;
			} else {
				STM32_I2C_DR(port) = 0xec;
			}
		}
	} else { /* I2C in peripheral receiver */
		if (i2c_sr1 & (STM32_I2C_SR1_BTF | STM32_I2C_SR1_RXNE))
			host_buffer[buf_idx++] = STM32_I2C_DR(port);
	}

	/* STOPF or AF */
	if (i2c_sr1 & (STM32_I2C_SR1_STOPF | STM32_I2C_SR1_AF)) {
		/* Disable buffer interrupt */
		STM32_I2C_CR2(port) &= ~STM32_I2C_CR2_ITBUFEN;

#ifdef CONFIG_BOARD_I2C_ADDR_FLAGS
		if (rx_pending &&
		    (addr_8b >> 1) ==
			    I2C_STRIP_FLAGS(CONFIG_BOARD_I2C_ADDR_FLAGS))
			i2c_process_board_command(0, addr_8bit, buf_idx);
#endif
		rx_pending = 0;
		tx_pending = 0;

		/* Clear AF */
		STM32_I2C_SR1(port) &= ~STM32_I2C_SR1_AF;
		/* Clear STOPF: read SR1 and write CR1 */
		unused = STM32_I2C_SR1(port);
		STM32_I2C_CR1(port) = i2c_cr1 | STM32_I2C_CR1_PE;

		/* No longer inhibit deep sleep after stop condition */
		enable_sleep(SLEEP_MASK_I2C_PERIPHERAL);
	}

	/* Enable again */
	if (!(i2c_cr1 & STM32_I2C_CR1_PE))
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_PE;
}
static void i2c_event_interrupt(void)
{
	i2c_event_handler(I2C_PORT_EC);
}
DECLARE_IRQ(IRQ_PERIPHERAL_EV, i2c_event_interrupt, 2);
DECLARE_IRQ(IRQ_PERIPHERAL_ER, i2c_event_interrupt, 2);
#endif

/* Init all available i2c ports */
void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_init_port(p);

#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS
	/* Enable ACK */
	STM32_I2C_CR1(I2C_PORT_EC) |= STM32_I2C_CR1_ACK;
	/* Enable interrupts */
	STM32_I2C_CR2(I2C_PORT_EC) |= STM32_I2C_CR2_ITEVTEN |
				      STM32_I2C_CR2_ITERREN;
	/* Setup host command peripheral */
	STM32_I2C_OAR1(I2C_PORT_EC) =
		STM32_I2C_OAR1_B14 |
		(I2C_STRIP_ADDR(CONFIG_HOSTCMD_I2C_ADDR_FLAGS) << 1);
#ifdef CONFIG_BOARD_I2C_ADDR_FLAGS
	STM32_I2C_OAR2(I2C_PORT_EC) =
		STM32_I2C_OAR2_ENDUAL |
		(I2C_STRIP_FLAGS(CONFIG_BOARD_I2C_ADDR_FLAGS) << 1);
#endif
	task_enable_irq(IRQ_PERIPHERAL_EV);
	task_enable_irq(IRQ_PERIPHERAL_ER);
#endif
}
