/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C drivers for STM32L4xx as well as STM32L5xx. */

#include "builtin/assert.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT_MASTER (10 * MSEC)

#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS
#define I2C_SLAVE_ERROR_CODE 0xec
#if (I2C_PORT_EC == STM32_I2C1_PORT)
#define IRQ_SLAVE STM32_IRQ_I2C1
#else
#define IRQ_SLAVE STM32_IRQ_I2C2
#endif
#endif

/* I2C port state data */
struct i2c_port_data {
	uint32_t timeout_us; /* Transaction timeout, or 0 to use default */
	enum i2c_freq freq; /* Port clock speed */
};
static struct i2c_port_data pdata[I2C_PORT_COUNT];

void i2c_set_timeout(int port, uint32_t timeout)
{
	pdata[port].timeout_us = timeout ? timeout : I2C_TX_TIMEOUT_MASTER;
}

/* timing register values for supported input clks / i2c clk rates */
static const uint32_t busyloop_us[I2C_FREQ_COUNT] = {
	[I2C_FREQ_1000KHZ] = 16, /* Enough for 2 bytes */
	[I2C_FREQ_400KHZ] = 40, /* Enough for 2 bytes */
	[I2C_FREQ_100KHZ] = 0, /* No busy looping at 100kHz (bus is slow) */
};

/**
 * Wait for ISR register to contain the specified mask.
 *
 * Returns EC_SUCCESS, EC_ERROR_TIMEOUT if timed out waiting, or
 * EC_ERROR_UNKNOWN if an error bit appeared in the status register.
 */
static int wait_isr(int port, int mask)
{
	uint32_t start = __hw_clock_source_read();
	uint32_t delta = 0;

	do {
		int isr = STM32_I2C_ISR(port);

		/* Check for errors */
		if (isr & (STM32_I2C_ISR_ARLO | STM32_I2C_ISR_BERR |
			   STM32_I2C_ISR_NACK))
			return EC_ERROR_UNKNOWN;

		/* Check for desired mask */
		if ((isr & mask) == mask)
			return EC_SUCCESS;

		delta = __hw_clock_source_read() - start;

		/**
		 * Depending on the bus speed, busy loop for a while before
		 * sleeping and letting other things run.
		 */
		if (delta >= busyloop_us[pdata[port].freq])
			crec_usleep(100);
	} while (delta < pdata[port].timeout_us);

	return EC_ERROR_TIMEOUT;
}

/* Supported i2c input clocks */
enum stm32_i2c_clk_src {
	I2C_CLK_SRC_48MHZ = 0,
	I2C_CLK_SRC_16MHZ = 1,
	I2C_CLK_SRC_COUNT,
};

/* timing register values for supported input clks / i2c clk rates
 *
 * These values are calculated using ST's STM32cubeMX tool
 */
static const uint32_t timingr_regs[I2C_CLK_SRC_COUNT][I2C_FREQ_COUNT] = {
	[I2C_CLK_SRC_48MHZ] = {
		[I2C_FREQ_1000KHZ] = 0x20000209,
		[I2C_FREQ_400KHZ] = 0x2010091A,
		[I2C_FREQ_100KHZ] = 0x20303E5D,
	},
	[I2C_CLK_SRC_16MHZ] = {
		[I2C_FREQ_1000KHZ] = 0x00000107,
		[I2C_FREQ_400KHZ] = 0x00100B15,
		[I2C_FREQ_100KHZ] = 0x00303D5B,
	},
};

int chip_i2c_set_freq(int port, enum i2c_freq freq)
{
	enum stm32_i2c_clk_src src = I2C_CLK_SRC_16MHZ;

	/* Disable port */
	STM32_I2C_CR1(port) = 0;
	STM32_I2C_CR2(port) = 0;
	/* Set clock frequency */
	STM32_I2C_TIMINGR(port) = timingr_regs[src][freq];
	/* Enable port */
	STM32_I2C_CR1(port) = STM32_I2C_CR1_PE;

	pdata[port].freq = freq;

	return EC_SUCCESS;
}

enum i2c_freq chip_i2c_get_freq(int port)
{
	return pdata[port].freq;
}

/**
 * Initialize on the specified I2C port.
 *
 * @param p		the I2c port
 */
static void i2c_init_port(const struct i2c_port_t *p)
{
	int port = p->port;
	uint32_t val;
	enum i2c_freq freq;

	/* Enable I2C clock */
	if (port == 3) {
		STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_I2C4EN;
	} else {
		STM32_RCC_APB1ENR1 |= 1 << (21 + port);
	}

	/*	Select HSI 16MHz as I2C clock source	*/
	if (port == 3) {
		val = STM32_RCC_CCIPR2;
		val &= ~STM32_RCC_CCIPR2_I2C4SEL_MSK;
		val |= STM32_RCC_CCIPR_I2C_HSI16
		       << STM32_RCC_CCIPR2_I2C4SEL_POS;
		STM32_RCC_CCIPR2 = val;
	} else {
		val = STM32_RCC_CCIPR;
		val &= ~(STM32_RCC_CCIPR_I2C1SEL_MASK << (port * 2));
		val |= STM32_RCC_CCIPR_I2C_HSI16
		       << (STM32_RCC_CCIPR_I2C1SEL_SHIFT + port * 2);
		STM32_RCC_CCIPR = val;
	}

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* Set clock frequency */
	switch (p->kbps) {
	case 1000:
		STM32_SYSCFG_CFGR1 |= STM32_SYSCFG_I2CFMP(port);
		freq = I2C_FREQ_1000KHZ;
		break;
	case 400:
		freq = I2C_FREQ_400KHZ;
		break;
	case 100:
		freq = I2C_FREQ_100KHZ;
		break;
	default: /* unknown speed, defaults to 100kBps */
		CPRINTS("I2C bad speed %d kBps", p->kbps);
		freq = I2C_FREQ_100KHZ;
	}

	/* Set up initial bus frequencies */
	chip_i2c_set_freq(p->port, freq);

	/* Set up default timeout */
	i2c_set_timeout(port, 0);
}

/*****************************************************************************/

#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS

static void i2c_event_handler(int port)
{
	/* Variables tracking the handler state.
	 * TODO: Should have as many sets of these variables as the number
	 * of slave ports.
	 */
	static int rx_pending, rx_idx;
	static int tx_pending, tx_idx, tx_end;
	static uint8_t slave_buffer[I2C_MAX_HOST_PACKET_SIZE + 2];
	int isr = STM32_I2C_ISR(port);

	/*
	 * Check for error conditions. Note, arbitration loss and bus error
	 * are the only two errors we can get as a slave allowing clock
	 * stretching and in non-SMBus mode.
	 */
	if (isr & (STM32_I2C_ISR_ARLO | STM32_I2C_ISR_BERR)) {
		rx_pending = 0;
		tx_pending = 0;

		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

		/* Clear error status bits */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_BERRCF |
				       STM32_I2C_ICR_ARLOCF;
	}

	/* Transfer matched our slave address */
	if (isr & STM32_I2C_ISR_ADDR) {
		if (isr & STM32_I2C_ISR_DIR) {
			/* Transmitter slave */
			/* Clear transmit buffer */
			STM32_I2C_ISR(port) |= STM32_I2C_ISR_TXE;

			if (rx_pending)
				/* RESTART */
				i2c_data_received(port, slave_buffer, rx_idx);
			tx_end = i2c_set_response(port, slave_buffer, rx_idx);
			tx_idx = 0;
			rx_pending = 0;
			tx_pending = 1;

			/* Enable txis interrupt to start response */
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_TXIE;
		} else {
			/* Receiver slave */
			rx_idx = 0;
			rx_pending = 1;
			tx_pending = 0;
		}

		/* Clear ADDR bit by writing to ADDRCF bit */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_ADDRCF;
		/* Inhibit stop mode when addressed until STOPF flag is set */
		disable_sleep(SLEEP_MASK_I2C_PERIPHERAL);
	}

	/*
	 * Receive buffer not empty
	 *
	 * When a master finishes sending data, it'll set STOP bit. It causes
	 * the slave to receive RXNE and STOP interrupt at the same time. So,
	 * we need to process RXNE first, then handle STOP.
	 */
	if (isr & STM32_I2C_ISR_RXNE)
		slave_buffer[rx_idx++] = STM32_I2C_RXDR(port);

	/* Stop condition on bus */
	if (isr & STM32_I2C_ISR_STOP) {
		if (rx_pending)
			i2c_data_received(port, slave_buffer, rx_idx);
		tx_idx = 0;
		tx_end = 0;
		rx_pending = 0;
		tx_pending = 0;
		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

		/* Clear STOPF bit by writing to STOPCF bit */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_STOPCF;

		/* No longer inhibit deep sleep after stop condition */
		enable_sleep(SLEEP_MASK_I2C_PERIPHERAL);
	}

	if (isr & STM32_I2C_ISR_NACK) {
		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;
		/* Clear NACK */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_NACKCF;
	}

	/* Transmitter empty event */
	if (isr & STM32_I2C_ISR_TXIS) {
		if (port == I2C_PORT_EC) {
			if (tx_pending) {
				if (tx_idx < tx_end) {
					STM32_I2C_TXDR(port) =
						slave_buffer[tx_idx++];
				} else {
					STM32_I2C_TXDR(port) =
						I2C_SLAVE_ERROR_CODE;
					tx_idx = 0;
					tx_end = 0;
					tx_pending = 0;
				}
			} else {
				STM32_I2C_TXDR(port) = I2C_SLAVE_ERROR_CODE;
			}
		}
	}
}

static void i2c_event_interrupt(void)
{
	i2c_event_handler(I2C_PORT_EC);
}
DECLARE_IRQ(IRQ_SLAVE, i2c_event_interrupt, 2);
#endif

/*****************************************************************************/
/* Interface */

int chip_i2c_xfer(const int port, const uint16_t addr_flags, const uint8_t *out,
		  int out_bytes, uint8_t *in, int in_bytes, int flags)
{
	int addr_8bit = I2C_STRIP_FLAGS(addr_flags) << 1;
	int rv = EC_SUCCESS;
	int i;
	int xfer_start = flags & I2C_XFER_START;
	int xfer_stop = flags & I2C_XFER_STOP;

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	/* Clear status */
	if (xfer_start) {
		STM32_I2C_ICR(port) = STM32_I2C_ICR_ALL;
		STM32_I2C_CR2(port) = 0;
	}

	if (out_bytes || !in_bytes) {
		/*
		 * Configure the write transfer: if we are stopping then set
		 * AUTOEND bit to automatically set STOP bit after NBYTES.
		 * if we are not stopping, set RELOAD bit so that we can load
		 * NBYTES again. if we are starting, then set START bit.
		 */
		STM32_I2C_CR2(port) =
			((out_bytes & 0xFF) << 16) | addr_8bit |
			((in_bytes == 0 && xfer_stop) ? STM32_I2C_CR2_AUTOEND :
							0) |
			((in_bytes == 0 && !xfer_stop) ? STM32_I2C_CR2_RELOAD :
							 0) |
			(xfer_start ? STM32_I2C_CR2_START : 0);

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
		/*
		 * Configure the read transfer: if we are stopping then set
		 * AUTOEND bit to automatically set STOP bit after NBYTES.
		 * if we are not stopping, set RELOAD bit so that we can load
		 * NBYTES again. if we were just transmitting, we need to
		 * set START bit to send (re)start and begin read transaction.
		 */
		STM32_I2C_CR2(port) =
			((in_bytes & 0xFF) << 16) | STM32_I2C_CR2_RD_WRN |
			addr_8bit | (xfer_stop ? STM32_I2C_CR2_AUTOEND : 0) |
			(!xfer_stop ? STM32_I2C_CR2_RELOAD : 0) |
			(out_bytes || xfer_start ? STM32_I2C_CR2_START : 0);

		for (i = 0; i < in_bytes; i++) {
			/* Wait for receive buffer not empty */
			rv = wait_isr(port, STM32_I2C_ISR_RXNE);
			if (rv)
				goto xfer_exit;

			in[i] = STM32_I2C_RXDR(port);
		}
	}

	/*
	 * If we are stopping, then we already set AUTOEND and we should
	 * wait for the stop bit to be transmitted. Otherwise, we set
	 * the RELOAD bit and we should wait for transfer complete
	 * reload (TCR).
	 */
	rv = wait_isr(port, xfer_stop ? STM32_I2C_ISR_STOP : STM32_I2C_ISR_TCR);
	if (rv)
		goto xfer_exit;

xfer_exit:
	/* clear status */
	if (xfer_stop)
		STM32_I2C_ICR(port) = STM32_I2C_ICR_ALL;

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

	if (get_scl_from_i2c_port(port, &g))
		/* If no SCL pin is defined, return 1 to appear idle. */
		return 1;

	return gpio_get_level(g);
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	if (get_sda_from_i2c_port(port, &g))
		/* If no SDA pin is defined, return 1 to appear idle. */
		return 1;

	return gpio_get_level(g);
}

int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
	       (i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_init_port(p);

#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS
	STM32_I2C_CR1(I2C_PORT_EC) |= STM32_I2C_CR1_RXIE | STM32_I2C_CR1_ERRIE |
				      STM32_I2C_CR1_ADDRIE |
				      STM32_I2C_CR1_STOPIE |
				      STM32_I2C_CR1_NACKIE;
	STM32_I2C_OAR1(I2C_PORT_EC) =
		0x8000 | (I2C_STRIP_FLAGS(CONFIG_HOSTCMD_I2C_ADDR_FLAGS) << 1);
	task_enable_irq(IRQ_SLAVE);
#endif
}
