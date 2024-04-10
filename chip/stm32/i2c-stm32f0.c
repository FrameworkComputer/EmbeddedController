/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "i2c.h"
#include "i2c_private.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT_CONTROLLER (10 * MSEC)

#ifdef CONFIG_HOSTCMD_I2C_ADDR_FLAGS
#if (I2C_PORT_EC == STM32_I2C1_PORT)
#define IRQ_PERIPHERAL STM32_IRQ_I2C1
#else
#define IRQ_PERIPHERAL STM32_IRQ_I2C2
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
	pdata[port].timeout_us = timeout ? timeout : I2C_TX_TIMEOUT_CONTROLLER;
}

/* timingr register values for supported input clks / i2c clk rates */
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
	I2C_CLK_SRC_8MHZ = 1,
	I2C_CLK_SRC_COUNT,
};

/* timingr register values for supported input clks / i2c clk rates */
static const uint32_t timingr_regs[I2C_CLK_SRC_COUNT][I2C_FREQ_COUNT] = {
	[I2C_CLK_SRC_48MHZ] = {
		[I2C_FREQ_1000KHZ] = 0x50100103,
		[I2C_FREQ_400KHZ] = 0x50330609,
		[I2C_FREQ_100KHZ] = 0xB0421214,
	},
	[I2C_CLK_SRC_8MHZ] = {
		[I2C_FREQ_1000KHZ] = 0x00100306,
		[I2C_FREQ_400KHZ] = 0x00310309,
		[I2C_FREQ_100KHZ] = 0x10420f13,
	},
};

int chip_i2c_set_freq(int port, enum i2c_freq freq)
{
	enum stm32_i2c_clk_src src = I2C_CLK_SRC_48MHZ;

#if defined(CONFIG_HOSTCMD_I2C_ADDR_FLAGS) && \
	defined(CONFIG_LOW_POWER_IDLE) && (I2C_PORT_EC == STM32_I2C1_PORT)
	if (port == STM32_I2C1_PORT) {
		/*
		 * Use HSI (8MHz) for i2c clock. This allows smooth wakeup
		 * from STOP mode since HSI is only clock running immediately
		 * upon exit from STOP mode.
		 */
		src = I2C_CLK_SRC_8MHZ;
	}
#endif

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
static int i2c_init_port(const struct i2c_port_t *p)
{
	int port = p->port;
	int ret = EC_SUCCESS;
	enum i2c_freq freq;

	/* Enable clocks to I2C modules if necessary */
	if (!(STM32_RCC_APB1ENR & (1 << (21 + port))))
		STM32_RCC_APB1ENR |= 1 << (21 + port);

	if (port == STM32_I2C1_PORT) {
#if defined(CONFIG_HOSTCMD_I2C_ADDR_FLAGS) && \
	defined(CONFIG_LOW_POWER_IDLE) && (I2C_PORT_EC == STM32_I2C1_PORT)
		/*
		 * Use HSI (8MHz) for i2c clock. This allows smooth wakeup
		 * from STOP mode since HSI is only clock running immediately
		 * upon exit from STOP mode.
		 */
		STM32_RCC_CFGR3 &= ~0x10;
#else
		/* Use SYSCLK for i2c clock. */
		STM32_RCC_CFGR3 |= 0x10;
#endif
	}

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* Set clock frequency */
	switch (p->kbps) {
	case 1000:
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
		ret = EC_ERROR_INVAL;
	}

	/* Set up initial bus frequencies */
	chip_i2c_set_freq(p->port, freq);

	/* Set up default timeout */
	i2c_set_timeout(port, 0);

	return ret;
}

/*****************************************************************************/
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
	STM32_I2C_CR1(host_i2c_resp_port) |= STM32_I2C_CR1_TXIE;
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

#ifdef TCPCI_I2C_PERIPHERAL
static void i2c_send_tcpc_response(int len)
{
	/* host_buffer data range, beyond this length, will return 0xec */
	tx_index = 0;
	tx_end = len;

	/* enable transmit interrupt and use irq to send data back */
	STM32_I2C_CR1(host_i2c_resp_port) |= STM32_I2C_CR1_TXIE;
}

static void i2c_process_tcpc_command(int read, int addr, int len)
{
	tcpc_i2c_process(read, TCPC_ADDR_TO_PORT(addr), len, &host_buffer[0],
			 i2c_send_tcpc_response);
}
#endif

static void i2c_event_handler(int port)
{
	int i2c_isr;
	static int rx_pending, buf_idx;
#ifdef TCPCI_I2C_PERIPHERAL
	int addr;
#endif

	i2c_isr = STM32_I2C_ISR(port);

	/*
	 * Check for error conditions. Note, arbitration loss and bus error
	 * are the only two errors we can get as a peripheral allowing clock
	 * stretching and in non-SMBus mode.
	 */
	if (i2c_isr & (STM32_I2C_ISR_ARLO | STM32_I2C_ISR_BERR)) {
		rx_pending = 0;
		tx_pending = 0;

		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

		/* Clear error status bits */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_BERRCF |
				       STM32_I2C_ICR_ARLOCF;
	}

	/* Transfer matched our peripheral address */
	if (i2c_isr & STM32_I2C_ISR_ADDR) {
		if (i2c_isr & STM32_I2C_ISR_DIR) {
			/* Transmitter peripheral */
			/* Clear transmit buffer */
			STM32_I2C_ISR(port) |= STM32_I2C_ISR_TXE;

			/* Enable txis interrupt to start response */
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_TXIE;
		} else {
			/* Receiver peripheral */
			buf_idx = 0;
			rx_pending = 1;
		}

		/* Clear ADDR bit by writing to ADDRCF bit */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_ADDRCF;
		/* Inhibit sleep mode when addressed until STOPF flag is set */
		disable_sleep(SLEEP_MASK_I2C_PERIPHERAL);
	}

	/* Receiver full event */
	if (i2c_isr & STM32_I2C_ISR_RXNE)
		host_buffer[buf_idx++] = STM32_I2C_RXDR(port);

	/* Stop condition on bus */
	if (i2c_isr & STM32_I2C_ISR_STOP) {
#ifdef TCPCI_I2C_PERIPHERAL
		/*
		 * if tcpc is being addressed, and we received a stop
		 * while rx is pending, then this is a write only to
		 * the tcpc.
		 */
		addr = STM32_I2C_ISR_ADDCODE(STM32_I2C_ISR(port));
		if (rx_pending && ADDR_IS_TCPC(addr))
			i2c_process_tcpc_command(0, addr, buf_idx);
#endif
		rx_pending = 0;
		tx_pending = 0;

		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

		/* Clear STOPF bit by writing to STOPCF bit */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_STOPCF;

		/* No longer inhibit deep sleep after stop condition */
		enable_sleep(SLEEP_MASK_I2C_PERIPHERAL);
	}

	/* Controller requested STOP or RESTART */
	if (i2c_isr & STM32_I2C_ISR_NACK) {
		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;
		/* Clear NACK */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_NACKCF;
		/* Resend last byte on RESTART */
		if (port == I2C_PORT_EC && tx_index)
			tx_index--;
	}

	/* Transmitter empty event */
	if (i2c_isr & STM32_I2C_ISR_TXIS) {
		if (port == I2C_PORT_EC) { /* host is waiting for PD response */
			if (tx_pending) {
				if (tx_index < tx_end) {
					STM32_I2C_TXDR(port) =
						host_buffer[tx_index++];
				} else {
					STM32_I2C_TXDR(port) = 0xec;
					/*
					 * Set tx_index = 0 to prevent NACK
					 * handler resending last buffer byte.
					 */
					tx_index = 0;
					tx_end = 0;
					/* No pending data */
					tx_pending = 0;
				}
			} else if (rx_pending) {
				host_i2c_resp_port = port;
				/*
				 * Disable TXIS interrupt, transmission will
				 * be prepared by host command task.
				 */
				STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

#ifdef TCPCI_I2C_PERIPHERAL
				addr = STM32_I2C_ISR_ADDCODE(
					STM32_I2C_ISR(port));
				if (ADDR_IS_TCPC(addr))
					i2c_process_tcpc_command(1, addr,
								 buf_idx);
				else
#endif
					i2c_process_command();

				/* Reset host buffer after end of transfer */
				rx_pending = 0;
				tx_pending = 1;
			} else {
				STM32_I2C_TXDR(port) = 0xec;
			}
		}
	}
}
static void i2c2_event_interrupt(void)
{
	i2c_event_handler(I2C_PORT_EC);
}
DECLARE_IRQ(IRQ_PERIPHERAL, i2c2_event_interrupt, 2);
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

#if defined(CONFIG_I2C_SCL_GATE_ADDR) && defined(CONFIG_I2C_SCL_GATE_PORT)
	if (port == CONFIG_I2C_SCL_GATE_PORT &&
	    addr_flags == CONFIG_I2C_SCL_GATE_ADDR_FLAGS)
		gpio_set_level(CONFIG_I2C_SCL_GATE_GPIO, 1);
#endif

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	/* Clear status */
	if (xfer_start) {
		uint32_t cr2 = STM32_I2C_CR2(port);

		STM32_I2C_ICR(port) = STM32_I2C_ICR_ALL;
		STM32_I2C_CR2(port) = 0;
		if (cr2 & STM32_I2C_CR2_RELOAD) {
			/*
			 * If I2C_XFER_START flag is on and we've set RELOAD=1
			 * in previous chip_i2c_xfer() call. Then we are
			 * probably in the middle of an i2c transaction.
			 *
			 * In this case, we need to clear the RELOAD bit and
			 * wait for Transfer Complete (TC) flag, to make sure
			 * the chip is not expecting another NBYTES data, And
			 * send repeated-start correctly.
			 */
			rv = wait_isr(port, STM32_I2C_ISR_TC);
			if (rv)
				goto xfer_exit;
		}
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
		 * This allows peripherals on the bus to detect bus-idle before
		 * the next start condition.
		 */
		udelay(10);
		/* re-initialize the controller */
		STM32_I2C_CR2(port) = 0;
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_PE;
		udelay(10);
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_PE;
	}

#ifdef CONFIG_I2C_SCL_GATE_ADDR
	if (port == CONFIG_I2C_SCL_GATE_PORT &&
	    addr_flags == CONFIG_I2C_SCL_GATE_ADDR_FLAGS)
		gpio_set_level(CONFIG_I2C_SCL_GATE_GPIO, 0);
#endif

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

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	return 1;
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
#if defined(CONFIG_LOW_POWER_IDLE) && (I2C_PORT_EC == STM32_I2C1_PORT)
	/*
	 * If using low power idle and EC port is I2C1, then set I2C1 to wake
	 * from STOP mode on address match. Note, this only works on I2C1 and
	 * only if the clock to I2C1 is HSI 8MHz.
	 */
	STM32_I2C_CR1(I2C_PORT_EC) |= STM32_I2C_CR1_WUPEN;
#endif
	STM32_I2C_OAR1(I2C_PORT_EC) =
		0x8000 | (I2C_STRIP_FLAGS(CONFIG_HOSTCMD_I2C_ADDR_FLAGS) << 1);
#ifdef TCPCI_I2C_PERIPHERAL
	/*
	 * Configure TCPC address with OA2[1] masked so that we respond
	 * to CONFIG_TCPC_I2C_BASE_ADDR and CONFIG_TCPC_I2C_BASE_ADDR + 2.
	 */
	STM32_I2C_OAR2(I2C_PORT_EC) =
		0x8100 |
		(I2C_STRIP_FLAGS(CONFIG_TCPC_I2C_BASE_ADDR_FLAGS) << 1);
#endif
	task_enable_irq(IRQ_PERIPHERAL);
#endif
}
